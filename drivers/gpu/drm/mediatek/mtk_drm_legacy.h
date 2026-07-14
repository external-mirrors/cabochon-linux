/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Support for legacy mediatek-drm display paths
 *
 * Please read mtk_drm_legacy.c for more information.
 *
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef MTK_DRM_LEGACY_H
#define MTK_DRM_LEGACY_H

struct mtk_drm_path_definition;

extern struct mtk_drm_path_definition mt2701_legacy_paths[];
extern struct mtk_drm_path_definition mt2712_legacy_paths[];
extern struct mtk_drm_path_definition mt7623_legacy_paths[];
extern struct mtk_drm_path_definition mt8167_legacy_paths[];
extern struct mtk_drm_path_definition mt8173_legacy_paths[];
extern struct mtk_drm_path_definition mt8183_legacy_paths[];
extern struct mtk_drm_path_definition mt8186_legacy_paths[];
extern struct mtk_drm_path_definition mt8188_legacy_paths[];
extern struct mtk_drm_path_definition mt8192_legacy_paths[];
extern struct mtk_drm_path_definition mt8195_vdo0_legacy_paths[];
extern struct mtk_drm_path_definition mt8195_vdo1_legacy_paths[];

int mtk_drm_legacy_inject_mutex_trig_ids(struct mtk_drm_comp_list *hlist,
					 struct device_node *mutex_node);
u8 mtk_drm_legacy_get_ovl_adaptor_mutex_trig_id(enum mtk_ddp_comp_type ddp_type,
						u8 ddp_inst_id,
						struct device_node *mutex_node);

void mtk_drm_legacy_ovl_adaptor_probe(struct device *dev, struct mtk_drm_private *priv,
				      struct component_match **match);

#endif /* MTK_DRM_LEGACY_H */
