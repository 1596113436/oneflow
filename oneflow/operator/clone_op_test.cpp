#include "operator/clone_op.h"
#include "operator/operator_manager.h"
#include "operator/op_util.h"
#include "gtest/gtest.h"

namespace oneflow {

TEST(CloneOp, clone_4x3_3_times) {
  OperatorConf op_conf;
  op_conf.set_name("clone_test");
  op_conf.mutable_clone_conf()->set_out_num(3);
  op_conf.mutable_clone_conf()->set_lbn("clone_test_lbn");
  auto clone_op = OpMgr::Singleton().ConstructOp(op_conf);

  HashMap<std::string, Shape*> bn2shape_ptr;
  std::vector<int64_t> shape_vec = {4, 3};
  bn2shape_ptr.insert(
      std::make_pair(clone_op->SoleIbn(), new Shape(shape_vec)));
  for(std::string obn : clone_op->output_bns()){
    bn2shape_ptr.insert(std::make_pair(obn, new Shape));
  }
  auto fp = [&bn2shape_ptr](const std::string& bn) {
    return bn2shape_ptr.at(bn);
  };

  clone_op->InferShape4FwBlobs(fp, kDataParallel, 3, 10);

  Shape* input_shape_ptr = bn2shape_ptr.at(clone_op->SoleIbn());
  for(std::string obn : clone_op->output_bns()){
    ASSERT_EQ(*input_shape_ptr, *bn2shape_ptr.at(obn));
    ASSERT_NE(input_shape_ptr, bn2shape_ptr.at(obn));
  }
}

} // namespace oneflow

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
