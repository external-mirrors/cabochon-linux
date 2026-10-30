// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek VideoDisplay Controller Interrupt Crossbar
 *
 * Copyright (C) 2026 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>

#define REG_VDISP_AO_INT_EN		0x0
#  define VDISP_AO_CPU_INT_EN		BIT(0)
#  define VDISP_AO_MODULE_INT_MERGE	BIT(5)
#define REG_VDISP_AO_INT_SEL_G0		0x20
#define REG_VDISP_AO_INT_SEL_G1		0x24

#define REG_SLOT_MASK			GENMASK(7, 0)
#define SLOTS_PER_REG			4
#define BITS_PER_SLOT			8

/**
 * struct irq_mtk_vdisp_slot - Interrupt crossbar slot
 * @hw_src_id: Identifier of the routed Hardware Interrupt
 * @virq:      VIRQ associated to this interrupt
 * @in_use:    Tracks whether this slot is used
 */
struct irq_mtk_vdisp_slot {
	u8 hw_src_id;
	unsigned int virq;
	bool in_use;
};

/**
 * struct irq_mtk_vdisp_pdata - SoC specific data
 * @num_regs:        Number of registers available for this crossbar
 * @num_irqs:        Total number of programmable interrupts in crossbar
 * @gic_hwirq_start: First GIC interrupt of the crossbar
 */
struct irq_mtk_vdisp_pdata {
	u8 num_regs;
	u8 num_irqs;
	u16 gic_hwirq_start;
};

/**
 * struct irq_mtk_vdisp - Main driver structure
 * @dev:         Pointer to struct device
 * @base:        MMIO for this controller
 * @domain:      Pointer to IRQ Domain
 * @lock:        Spin lock to serialize HW access and slots structure
 * @hwdata:      SoC specific data
 * @num_slots:   Number of total interrupt crossbar slots
 * @slots:       Linear array of 8-bits interrupt crossbar slots
 */
static struct irq_mtk_vdisp {
	struct device *dev;
	void __iomem *base;
	struct irq_domain *domain;
	raw_spinlock_t lock;
	const struct irq_mtk_vdisp_pdata *hwdata;

	size_t num_slots;
	struct irq_mtk_vdisp_slot slots[] __counted_by(num_slots);
} *mtk_vdisp;

/**
 * irq_mtk_vdisp_find_free_slot() - Find a free crossbar slot in controller
 * @vdisp: Main driver structure
 * 
 * Return: Number of slot with at least one free entry, or -ENOENT if not found
 */
static size_t irq_mtk_vdisp_find_free_slot(struct irq_mtk_vdisp *vdisp)
{
	size_t i;

	for (i = 0; i < vdisp->num_slots; i++) {
		if (!vdisp->slots[i].in_use)
			return i;
	}

	return -ENOENT;
}

/**
 * irq_mtk_vdisp_program_hw_slot() - Set interrupt routing for slot
 * @vdisp:    Main driver structure
 * @irq_slot: Interrupt slot to program
 * @src_id:   Hardware interrupt to set in slot
 *
 * Note: Must be called with spinlock held!
 */
static void irq_mtk_vdisp_program_hw_slot(struct irq_mtk_vdisp *vdisp,
					  int irq_slot, u32 src_id)
{
	unsigned int bit = (irq_slot % SLOTS_PER_REG) * BITS_PER_SLOT;
	unsigned int reg = irq_slot / SLOTS_PER_REG;
	unsigned int offset = reg * SLOTS_PER_REG;
	u32 val;

	val = readl(vdisp->base + REG_VDISP_AO_INT_SEL_G0 + offset);
	val &= ~(REG_SLOT_MASK << bit);
	val |= (src_id << bit);
	writel(val, vdisp->base + REG_VDISP_AO_INT_SEL_G0 + offset);
}

static struct irq_chip mtk_vdisp_irqchip = {
	.name		= "mediatek-vdisp",
	.irq_startup	= irq_chip_startup_parent,
	.irq_mask	= irq_chip_mask_parent,
	.irq_unmask	= irq_chip_unmask_parent,
	.irq_eoi	= irq_chip_eoi_parent,
	.irq_retrigger	= irq_chip_retrigger_hierarchy,
	.irq_set_type	= irq_chip_set_type_parent,
	.irq_set_affinity = IS_ENABLED(CONFIG_SMP) ? irq_chip_set_affinity_parent : NULL,
};

static int irq_mtk_vdisp_domain_translate(struct irq_domain *d, struct irq_fwspec *fwspec,
					  unsigned long *out_hwirq, unsigned int *out_type)
{
	/* Only SPI supported, PPIs cannot be routed by crossbar */
	if (fwspec->param_count == 3 && fwspec->param[0] != GIC_SPI)
		return -EINVAL;

	return irq_domain_translate_twothreecell(d, fwspec, out_hwirq, out_type);
}

static int irq_mtk_vdisp_domain_alloc(struct irq_domain *domain, unsigned int virq,
				      unsigned int nr_irqs, void *arg)
{
	struct irq_mtk_vdisp *vdisp = domain->host_data;
	struct device *dev = vdisp->dev;
	struct irq_fwspec *fwspec = arg;
	struct irq_fwspec parent_fwspec;
	unsigned long hw_src_id;
	unsigned int gic_irq;
	unsigned int type;
	size_t slot_idx;
	int ret;

	ret = irq_mtk_vdisp_domain_translate(domain, fwspec, &hw_src_id, &type);
	if (ret) {
		dev_err(dev, "IRQ Translate failed\n");
		return ret;
	}

	if (hw_src_id == 0 || hw_src_id >= REG_SLOT_MASK) {
		dev_err(dev, "Invalid source ID %lu.\n", hw_src_id);
		return -EINVAL;
	}

	scoped_guard(raw_spinlock, &vdisp->lock) {
		/* Find a free slot in the crossbar... */
		slot_idx = irq_mtk_vdisp_find_free_slot(vdisp);
		if (slot_idx < 0) {
			dev_err(dev, "All IRQ slots are full!\n");
			return slot_idx;
		}

		/* ...set the crossbar registers to route the HW interrupt to GIC... */
		irq_mtk_vdisp_program_hw_slot(vdisp, slot_idx, (u32)hw_src_id);

		/* ...and finally keep track of what gets allocated. */
		vdisp->slots[slot_idx].in_use = true;
		vdisp->slots[slot_idx].hw_src_id = hw_src_id;
		vdisp->slots[slot_idx].virq = virq;
	}
	gic_irq = vdisp->hwdata->gic_hwirq_start + slot_idx;

	dev_dbg(dev, "HW Interrupt %lu routed to GIC IRQ %u\n", hw_src_id, gic_irq);

	/* Crossbar successfully set to route the interrupt, wire it up! */
	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 3;
	parent_fwspec.param[0] = GIC_SPI;
	parent_fwspec.param[1] = gic_irq;
	parent_fwspec.param[2] = type;

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hw_src_id,
					    &mtk_vdisp_irqchip, vdisp);
	if (ret)
		goto error;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &parent_fwspec);
	if (ret)
		goto error;

	return 0;
error:
	guard(raw_spinlock_irqsave)(&vdisp->lock);

	vdisp->slots[slot_idx].in_use = false;
	irq_mtk_vdisp_program_hw_slot(vdisp, slot_idx, 0);

	return ret;
}

static void irq_mtk_vdisp_domain_free(struct irq_domain *domain,
				      unsigned int virq, unsigned int nr_irqs)
{
	struct irq_mtk_vdisp *vdisp = domain->host_data;
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);
	unsigned long hw_src_id = data->hwirq;
	int i;

	scoped_guard(raw_spinlock, &vdisp->lock) {
		for (i = 0; i < vdisp->num_slots; i++) {
			if (!vdisp->slots[i].in_use || vdisp->slots[i].hw_src_id != hw_src_id)
				continue;

			irq_mtk_vdisp_program_hw_slot(vdisp, i, 0);
			vdisp->slots[i].in_use = false;
			vdisp->slots[i].hw_src_id = 0;
			vdisp->slots[i].virq = 0;
			break;
		}
	}

	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static const struct irq_domain_ops irq_mtk_vdisp_domain_ops = {
	.alloc     = irq_mtk_vdisp_domain_alloc,
	.free      = irq_mtk_vdisp_domain_free,
	.translate = irq_mtk_vdisp_domain_translate,
};

static int irq_mtk_vdisp_suspend(void *data)
{
	/* Disable the crossbar to allow power saving */
	writel(0, mtk_vdisp->base + REG_VDISP_AO_INT_EN);

	return 0;
}

static void irq_mtk_vdisp_resume(void *data)
{
	int i;

	/*
	 * If power to VDISP was cut, its registers will be reset to defaults:
	 * program the crossbar again based on the interrupts slots state from
	 * before entering suspend.
	 */
	for (i = 0; i < mtk_vdisp->hwdata->num_regs; i++) {
		bool anything_in_use = false;
		u32 val = 0;
		int j;

		for (j = 0; j < SLOTS_PER_REG; j++) {
			size_t slot_idx = (i * SLOTS_PER_REG) + j;
			u8 src_id = 0;

			if (mtk_vdisp->slots[slot_idx].in_use) {
				src_id = (u8)mtk_vdisp->slots[slot_idx].hw_src_id;
				anything_in_use = true;
			}

			val |= ((u32)src_id << (j * BITS_PER_SLOT));
		}

		if (anything_in_use)
			writel(val, mtk_vdisp->base + (i * 4));
	}

	/* Enable dispatching interrupts to the CPU */
	writel(VDISP_AO_CPU_INT_EN, mtk_vdisp->base + REG_VDISP_AO_INT_EN);
}

static const struct syscore_ops irq_mtk_vdisp_syscore_ops = {
	.suspend = irq_mtk_vdisp_suspend,
	.resume = irq_mtk_vdisp_resume,
};

static struct syscore irq_mtk_vdisp_syscore = {
	.ops = &irq_mtk_vdisp_syscore_ops,
};

static void irq_mtk_vdisp_remove(void *data)
{
	struct irq_mtk_vdisp *vdisp = data;

	irq_domain_remove(vdisp->domain);
}

static int irq_mtk_vdisp_probe_common(struct platform_device *pdev, struct device_node *parent,
				      const struct irq_mtk_vdisp_pdata *hwdata)
{
	struct device *dev = &pdev->dev;
	struct irq_mtk_vdisp *vdisp;
	struct irq_domain *domain;
	size_t slot_num = 0;
	size_t total_slots;
	int i, ret;

	/*
	 * Find the parent domain for creating the hierarchy later: this is
	 * done before allocating the vdisp structure to avoid continuously
	 * allocating and freeing, as that's practically the only thing that
	 * is more likely to fail in here.
	 */
	domain = irq_find_host(parent);
	if (!domain)
		return dev_err_probe(dev, -ENODEV, "Cannot find parent domain\n");

	/*
	 * Calculate the number of available 8-bits slots, where one slot
	 * corresponds to one 8-bits field in a crossbar register.
	 */
	total_slots = (hwdata->num_irqs - 2) / (hwdata->num_regs - 1);
	total_slots *= hwdata->num_regs;

	mtk_vdisp = devm_kzalloc(dev, struct_size(mtk_vdisp, slots, total_slots), GFP_KERNEL);
	if (!mtk_vdisp)
		return -ENOMEM;

	vdisp = mtk_vdisp;
	vdisp->num_slots = total_slots;
	vdisp->hwdata = hwdata;
	vdisp->dev = dev;

	raw_spin_lock_init(&vdisp->lock);
	platform_set_drvdata(pdev, vdisp);

	vdisp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vdisp->base))
		return dev_err_probe(dev, PTR_ERR(vdisp->base),
				     "Cannot ioremap registers\n");

	/* Make sure the crossbar registers are accessible */
	ret = devm_pm_runtime_set_active_enabled(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot enable Runtime PM\n");

	for (i = 0; i < hwdata->num_regs; i++) {
		int j;

		/* Each register routes four GIC lines */
		for (j = 0; j < SLOTS_PER_REG; j++, slot_num++)
			vdisp->slots[slot_num].in_use = false;

		writel(0, vdisp->base + (i * 4));
	}

	vdisp->domain = irq_domain_create_hierarchy(domain, 0, 0, dev_fwnode(dev),
						    &irq_mtk_vdisp_domain_ops, vdisp);
	if (!vdisp->domain)
		return dev_err_probe(dev, -ENOMEM, "Could not create domain hierarchy\n");

	ret = devm_add_action_or_reset(dev, irq_mtk_vdisp_remove, vdisp);
	if (ret)
		return ret;

	/* All done - start the crossbar now! */
	writel(VDISP_AO_CPU_INT_EN, vdisp->base + REG_VDISP_AO_INT_EN);

	register_syscore(&irq_mtk_vdisp_syscore);

	return 0;
}

/*
 * Registers are laid out like follows:
 *
 *             [24-31]   [16-23]   [8-15]     [0-7]
 *           ----------.---------.---------.---------.
 *           | HWIRQ-n | HWIRQ-2 | HWIRQ-1 | HWIRQ-0 |
 *           ----------'---------'---------'---------'
 *
 * Each register in the crossbar carries four interrupts, each of which is
 * separately routed to GIC as (gic_irq_start_number + vdisp_hwirq_index).
 *
 * For example, on MT8196, HWIRQ-0 routes the HW IP interrupt to GIC 450,
 * HWIRQ-1 to GIC 451, and so on.
 */
static int irq_mtk_vdisp_probe_mt8196(struct platform_device *pdev, struct device_node *parent)
{
	static const struct irq_mtk_vdisp_pdata irq_mtk_vdisp_mt8196 = {
		/* Each register has four interrupts, last one has only two */
		.num_regs = 7,
		.num_irqs = (6 * SLOTS_PER_REG) + 2,
		.gic_hwirq_start = 450,
	};

	return irq_mtk_vdisp_probe_common(pdev, parent, &irq_mtk_vdisp_mt8196);
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(irq_mtk_vdisp)
IRQCHIP_MATCH("mediatek,mt8196-vdisp-irq", irq_mtk_vdisp_probe_mt8196)
IRQCHIP_PLATFORM_DRIVER_END(irq_mtk_vdisp)
MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek VDisp Interrupt Crossbar Driver");
MODULE_LICENSE("GPL");
