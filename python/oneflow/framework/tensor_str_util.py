"""
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""
"""
This file is mostly referenced from PyTorch v1.8.1 torch/_tensor_str.py
"""
import os
import oneflow as flow
from typing import Optional, Tuple

def slice_util(tensor, slice_tuple: Tuple[int, int, int]):
    ndim = tensor.ndim
    slice_tuple_list = [slice_tuple] + [[None, None, None]] * (ndim - 1)
    if tensor.is_consistent:
        # input is s0, output is p
        # input is b, output is b
        # input is p, output is p
        # so 'to b' is not needed here
        tensor = flow.logical_slice(tensor, slice_tuple_list)
    else:
        tensor = flow.slice(tensor, slice_tuple_list)
    # flow.sequeeze will fail in some consistent tensor case
    if tensor.shape[0] == 1 and ndim > 1:
        tensor = tensor.reshape(list(tensor.shape[1:]))
    return tensor


def _autoset_linewidth():
    # os.terminal_size(columns, lines),
    # columns represents width of the terminal window in characters
    # and lines represents height of the terminal window in characters.
    try:
        linewidth = os.get_terminal_size()[0]
    except OSError:
        linewidth = 80
    return linewidth


def _try_convert_to_local_tensor(tensor):
    if tensor.is_consistent:
        tensor = tensor.to_consistent(
            placement=flow.env.all_device_placement(tensor.placement.device_type),
            sbp=flow.sbp.broadcast,
        ).to_local()
    return tensor