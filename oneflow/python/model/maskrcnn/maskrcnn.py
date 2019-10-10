# must import get cfg before importing oneflow
from config import get_default_cfgs

import oneflow as flow

from backbone import Backbone
from rpn import RPNHead, RPNLoss, RPNProposal
from box_head import BoxHead
from mask_head import MaskHead

import argparse

parser = argparse.ArgumentParser()
parser.add_argument(
    "--config_file", "-c", default=None, type=str, help="yaml config file"
)
parser.add_argument(
    "-load", "--model_load_dir", type=str, default="", required=False
)
parser.add_argument(
    "-g", "--gpu_num_per_node", type=int, default=1, required=False
)
parser.add_argument(
    "-d",
    "--debug",
    type=bool,
    default=True,
    required=False,
    help="debug with random data generated by numpy",
)
parser.add_argument(
    "-rpn", "--rpn_only", default=False, action="store_true", required=False
)
parser.add_argument(
    "-md", "--mock_dataset", default=False, action="store_true", required=False
)
parser.add_argument(
    "-rcnn_eval",
    "--rcnn_eval",
    default=False,
    action="store_true",
    required=False,
)

terminal_args = parser.parse_args()


debug_data = None

if terminal_args.mock_dataset:
    from mock_data import MockData

    debug_data = MockData("/tmp/shared_with_zwx/data_600x100_2_image.pkl", 64)


def get_numpy_placeholders():
    import numpy as np

    (N, H, W, C) = (2, 64, 64, 3)
    R = 50
    G = 12
    return {
        "images": np.random.randn(N, H, W, C).astype(np.float32),
        "image_sizes": np.random.randn(N, 2).astype(np.int32),
        "gt_boxes": np.random.randn(N, R, 4).astype(np.float32),
        "gt_segms": np.random.randn(N, G, 28, 28).astype(np.int8),
        "gt_labels": np.random.randn(N, G).astype(np.int32),
        "rpn_proposals": np.random.randn(1000, 4).astype(np.float32),
        "fpn_feature_map1": np.random.randn(1, 256, 512, 512).astype(
            np.float32
        ),
        "fpn_feature_map2": np.random.randn(1, 256, 256, 256).astype(
            np.float32
        ),
        "fpn_feature_map3": np.random.randn(1, 256, 128, 128).astype(
            np.float32
        ),
        "fpn_feature_map4": np.random.randn(1, 256, 64, 64).astype(np.float32),
    }


placeholders = get_numpy_placeholders()


def maskrcnn_train(images, image_sizes, gt_boxes, gt_segms, gt_labels):
    r"""Mask-RCNN
    Args:
    images: (N, H, W, C)
    image_sizes: (N, 2)
    gt_boxes: (N, R, 4), dynamic
    gt_segms: (N, G, 28, 28), dynamic
    gt_labels: (N, G), dynamic
    """
    assert images.is_dynamic == True
    assert image_sizes.is_dynamic == False
    assert gt_boxes.num_of_lod_levels == 2
    assert gt_segms.num_of_lod_levels == 2
    assert gt_labels.num_of_lod_levels == 2
    cfg = get_default_cfgs()
    if terminal_args.config_file is not None:
        cfg.merge_from_file(terminal_args.config_file)
    cfg.freeze()
    print(cfg)
    backbone = Backbone(cfg)
    rpn_head = RPNHead(cfg)
    rpn_loss = RPNLoss(cfg)
    rpn_proposal = RPNProposal(cfg)
    box_head = BoxHead(cfg)
    mask_head = MaskHead(cfg)

    image_size_list = [
        flow.squeeze(
            flow.local_gather(image_sizes, flow.constant(i, dtype=flow.int32)),
            [0],
        )
        for i in range(image_sizes.shape[0])
    ]
    gt_boxes_list = flow.piece_slice(gt_boxes, cfg.TRAINING_CONF.IMG_PER_GPU)
    gt_labels_list = flow.piece_slice(gt_labels, cfg.TRAINING_CONF.IMG_PER_GPU)
    gt_segms_list = flow.piece_slice(gt_segms, cfg.TRAINING_CONF.IMG_PER_GPU)
    anchors = []
    for i in range(cfg.DECODER.FPN_LAYERS):
        anchors.append(
            flow.detection.anchor_generate(
                images=flow.transpose(images, perm=[0, 2, 3, 1]),
                feature_map_stride=cfg.DECODER.FEATURE_MAP_STRIDE * pow(2, i),
                aspect_ratios=cfg.DECODER.ASPECT_RATIOS,
                anchor_scales=cfg.DECODER.ANCHOR_SCALES * pow(2, i),
            )
        )

    # Backbone
    # CHECK_POINT: fpn features
    features = backbone.build(images)

    # RPN
    cls_logit_list, bbox_pred_list = rpn_head.build(features)
    rpn_bbox_loss, rpn_objectness_loss = rpn_loss.build(
        anchors, image_size_list, gt_boxes_list, bbox_pred_list, cls_logit_list
    )
    if terminal_args.rpn_only:
        return rpn_bbox_loss, rpn_objectness_loss

    proposals = rpn_proposal.build(
        anchors, cls_logit_list, bbox_pred_list, image_size_list, gt_boxes_list
    )

    # Box Head
    box_loss, cls_loss, pos_proposal_list, pos_gt_indices_list = box_head.build_train(
        proposals, gt_boxes_list, gt_labels_list, features
    )

    # Mask Head
    mask_loss = mask_head.build_train(
        pos_proposal_list,
        pos_gt_indices_list,
        gt_segms_list,
        gt_labels_list,
        features,
    )

    return rpn_bbox_loss, rpn_objectness_loss, box_loss, cls_loss, mask_loss


def maskrcnn_eval(images, image_sizes):
    cfg = get_default_cfgs()
    if terminal_args.config_file is not None:
        cfg.merge_from_file(terminal_args.config_file)
    cfg.freeze()
    print(cfg)
    backbone = Backbone(cfg)
    rpn_head = RPNHead(cfg)
    rpn_proposal = RPNProposal(cfg)
    box_head = BoxHead(cfg)
    mask_head = MaskHead(cfg)

    image_size_list = [
        flow.squeeze(
            flow.local_gather(image_sizes, flow.constant(i, dtype=flow.int32)),
            [0],
        )
        for i in range(image_sizes.shape[0])
    ]
    anchors = []
    for i in range(cfg.DECODER.FPN_LAYERS):
        anchors.append(
            flow.detection.anchor_generate(
                images=images,
                feature_map_stride=cfg.DECODER.FEATURE_MAP_STRIDE * pow(2, i),
                aspect_ratios=cfg.DECODER.ASPECT_RATIOS,
                anchor_scales=cfg.DECODER.ANCHOR_SCALES * pow(2, i),
            )
        )

    # Backbone
    features = backbone.build(images)

    # RPN
    cls_logit_list, bbox_pred_list = rpn_head.build(features)
    proposals = rpn_proposal.build(
        anchors, cls_logit_list, bbox_pred_list, image_size_list, None
    )

    # Box Head
    cls_logits, box_pred = box_head.build_eval(proposals, features)

    # Mask Head
    mask_logits = mask_head.build_eval(proposals, features)

    return cls_logits, box_pred, mask_logits


if terminal_args.mock_dataset:

    @flow.function
    def mock_train(
        images=debug_data.blob_def("images"),
        image_sizes=debug_data.blob_def("image_size"),
        gt_boxes=debug_data.blob_def("gt_bbox"),
        gt_segms=debug_data.blob_def("gt_segm"),
        gt_labels=debug_data.blob_def("gt_labels"),
    ):
        flow.config.train.primary_lr(0.00001)
        flow.config.train.model_update_conf(dict(naive_conf={}))
        outputs = maskrcnn_train(
            images, image_sizes, gt_boxes, gt_segms, gt_labels
        )
        for loss in outputs:
            flow.losses.add_loss(loss)
        return outputs


if terminal_args.rcnn_eval:

    @flow.function
    def debug_rcnn_eval(
        rpn_proposals=flow.input_blob_def(
            placeholders["rpn_proposals"].shape,
            dtype=flow.float32,
            is_dynamic=True,
        ),
        fpn_fm1=flow.input_blob_def(
            placeholders["fpn_feature_map1"].shape,
            dtype=flow.float32,
            is_dynamic=True,
        ),
        fpn_fm2=flow.input_blob_def(
            placeholders["fpn_feature_map2"].shape,
            dtype=flow.float32,
            is_dynamic=True,
        ),
        fpn_fm3=flow.input_blob_def(
            placeholders["fpn_feature_map3"].shape,
            dtype=flow.float32,
            is_dynamic=True,
        ),
        fpn_fm4=flow.input_blob_def(
            placeholders["fpn_feature_map4"].shape,
            dtype=flow.float32,
            is_dynamic=True,
        ),
    ):
        cfg = get_default_cfgs()
        if terminal_args.config_file is not None:
            cfg.merge_from_file(terminal_args.config_file)
        cfg.freeze()
        print(cfg)
        box_head = BoxHead(cfg)
        image_ids = flow.concat(
            flow.detection.extract_piece_slice_id([rpn_proposals]), axis=0
        )
        x = box_head.box_feature_extractor(
            rpn_proposals, image_ids, [fpn_fm1, fpn_fm2, fpn_fm3, fpn_fm4]
        )
        cls_logits, box_pred = box_head.box_predictor(x)
        return cls_logits, box_pred


if __name__ == "__main__":
    flow.config.gpu_device_num(terminal_args.gpu_num_per_node)
    flow.config.ctrl_port(19878)

    flow.config.default_data_type(flow.float)
    check_point = flow.train.CheckPoint()
    if not terminal_args.model_load_dir:
        check_point.init()
    else:
        check_point.load(terminal_args.model_load_dir)
    if terminal_args.debug:
        if terminal_args.rcnn_eval:
            import numpy as np

            rpn_proposals = np.load(
                "/home/xfjiang/rcnn_eval_fake_data/rpn_proposals.npy"
            )
            fpn_feature_map1 = np.load(
                "/home/xfjiang/rcnn_eval_fake_data/fpn_fm1.npy"
            )
            fpn_feature_map2 = np.load(
                "/home/xfjiang/rcnn_eval_fake_data/fpn_fm2.npy"
            )
            fpn_feature_map3 = np.load(
                "/home/xfjiang/rcnn_eval_fake_data/fpn_fm3.npy"
            )
            fpn_feature_map4 = np.load(
                "/home/xfjiang/rcnn_eval_fake_data/fpn_fm4.npy"
            )
            results = debug_rcnn_eval(
                rpn_proposals,
                fpn_feature_map1,
                fpn_feature_map2,
                fpn_feature_map3,
                fpn_feature_map4,
            ).get()
            print(results)
        elif terminal_args.mock_dataset:
            if terminal_args.rpn_only:
                print(
                    "{:>8} {:>16} {:>16}".format(
                        "iter", "rpn_bbox_loss", "rpn_obj_loss"
                    )
                )
            else:
                print(
                    "{:>8} {:>16} {:>16} {:>16} {:>16} {:>16}".format(
                        "iter",
                        "rpn_bbox_loss",
                        "rpn_obj_loss",
                        "box_loss",
                        "cls_loss",
                        "mask_loss",
                    )
                )
            for i in range(10):
                train_loss = mock_train(
                    debug_data.blob("images"),
                    debug_data.blob("image_size"),
                    debug_data.blob("gt_bbox"),
                    debug_data.blob("gt_segm"),
                    debug_data.blob("gt_labels"),
                ).get()
                fmt_str = "{:>8} " + "{:>16.10f} " * len(train_loss)
                print_loss = [i]
                for loss in train_loss:
                    print_loss.append(loss.mean())
                print(fmt_str.format(*print_loss))
