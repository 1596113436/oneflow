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
import os
import unittest
import numpy as np

import oneflow as flow
import oneflow.unittest


@unittest.skipIf(os.getenv("ONEFLOW_TEST_CPU_ONLY"), "only test cpu cases")
@flow.unittest.skip_unless_1n1d()
class TestGraphWithEagerTensorCaught(oneflow.unittest.TestCase):
    def test_eager_tensor_forward_graph(test_case):
        class MyModuleWithEagerTensorForward(flow.nn.Module):
            def __init__(self):
                super().__init__()
                self.linear = flow.nn.Linear(3, 8, False)

            def forward(self, x):
                y0 = self.linear(x)
                eager_t = flow.tensor([1.0], dtype=y0.dtype, device=y0.device)
                out = y0 + eager_t
                return out

        my_net_module = MyModuleWithEagerTensorForward()
        flow.nn.init.constant_(my_net_module.linear.weight, 2.3)
        x = np.random.randn(5, 3)
        x = flow.tensor(x, dtype=flow.float32)

        class GraphEagerTensorCaught(flow.nn.Graph):
            def __init__(self):
                super().__init__()
                self.my_net = my_net_module

            def build(self, x):
                return self.my_net(x)

        my_g = GraphEagerTensorCaught()
        graph_out = my_g(x)
        eager_out = my_net_module(x)
        test_case.assertTrue(
            np.allclose(graph_out.numpy(), eager_out.numpy(), atol=1e-4, rtol=1e-4)
        )

    def test_eager_tensor_to(test_case):
        class EagerTensorToModule(flow.nn.Module):
            def __init__(self):
                super().__init__()

            def forward(self):
                # test free eager tensor to
                t = flow.tensor([1.0], dtype=flow.float32).to("cuda")
                return t

        e_m = EagerTensorToModule()

        class EagerTensorToGraph(flow.nn.Graph):
            def __init__(self):
                super().__init__()
                self.e_m = e_m

            def build(self):
                return self.e_m()

        e_g = EagerTensorToGraph()
        graph_out = e_g()
        eager_out = e_m()
        test_case.assertTrue(
            np.allclose(graph_out.numpy(), eager_out.numpy(), atol=1e-4, rtol=1e-4)
        )

    def test_two_graph_caught_same_free_eager_tensor(test_case):
        np_x = np.random.randn(5, 3)
        np_y = np.random.randn(5, 3)
        x = flow.tensor(np_x, dtype=flow.float32)
        y = flow.tensor(np_y, dtype=flow.float32)

        class GraphAdd(flow.nn.Graph):
            def __init__(self):
                super().__init__()

            def build(self):
                return x + y

        class GraphMul(flow.nn.Graph):
            def __init__(self):
                super().__init__()

            def build(self):
                return x * y

        g_add = GraphAdd()
        g_mul = GraphMul()

        add_out = g_add()
        mul_out = g_mul()
        test_case.assertTrue(
            np.allclose(add_out.numpy(), np_x + np_y, atol=1e-4, rtol=1e-4)
        )
        test_case.assertTrue(
            np.allclose(mul_out.numpy(), np_x * np_y, atol=1e-4, rtol=1e-4)
        )


@unittest.skipIf(os.getenv("ONEFLOW_TEST_CPU_ONLY"), "only test cpu cases")
@flow.unittest.skip_unless_1n2d()
class ConsistentFreeEagerTensorGraphTestCase(oneflow.unittest.TestCase):
    def test_consistent_eager_tensor_to(test_case):
        rank = flow.env.get_rank()
        placement = flow.placement("cpu", {0: [0, 1]})
        t_l = flow.tensor([1.0, 2.0], dtype=flow.float32)
        t = t_l.to_consistent(placement=placement, sbp=flow.sbp.broadcast)

        class ConsistentEagerTensorToModule(flow.nn.Module):
            def __init__(self):
                super().__init__()

            def forward(self):
                # test free eager tensor to
                nonlocal t
                t = t.to("cuda")
                return t

        e_m = ConsistentEagerTensorToModule()

        class ConsistentEagerTensorToGraph(flow.nn.Graph):
            def __init__(self):
                super().__init__()
                self.e_m = e_m

            def build(self):
                return self.e_m()

        e_g = ConsistentEagerTensorToGraph()
        graph_out = e_g().to_local()
        print("g ", graph_out.numpy())
        test_case.assertTrue(
            np.allclose(graph_out.numpy(), t_l.numpy(), atol=1e-4, rtol=1e-4)
        )


if __name__ == "__main__":
    unittest.main()
