/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "src/runtime/kernel/arm/base/quant_dtype_cast.h"
#include <vector>
#include "src/runtime/kernel/arm/nnacl/int8/quant_dtype_cast.h"
#include "src/runtime/runtime_api.h"
#include "src/kernel_registry.h"
#include "schema/model_generated.h"
#include "include/errorcode.h"

using mindspore::kernel::KERNEL_ARCH::kCPU;
using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_QuantDTypeCast;

namespace mindspore::kernel {
namespace {
constexpr int kQuantDTypeCastInputNum = 1;
constexpr int kQuantDTypeCastOutputNum = 1;
}  // namespace

int QuantDTypeCastCPUKernel::Init() {
  if (inputs_.size() != 1) {
    MS_LOG(ERROR) << "inputs number should be 1, but " << inputs_.size() << " is given.";
    return RET_ERROR;
  }
  if (outputs_.size() != 1) {
    MS_LOG(ERROR) << "outputs number should be 1, but " << inputs_.size() << " is given.";
    return RET_ERROR;
  }
  auto in_tensor = inputs_.front();
  auto out_tensor = outputs_.front();
  auto param = reinterpret_cast<QuantDTypeCastParameter *>(opParameter);
  if (param->srcT == kNumberTypeFloat32 && param->dstT == kNumberTypeInt8) {
    if (in_tensor->data_type() != kNumberTypeFloat32 || out_tensor->data_type() != kNumberTypeInt8) {
      MS_LOG(ERROR) << "param data type and tensor data type do not match.";
      return RET_ERROR;
    }
    inverse_ = false;
  } else if (param->srcT == kNumberTypeInt8 && param->dstT == kNumberTypeFloat32) {
    if (in_tensor->data_type() != kNumberTypeInt8 || out_tensor->data_type() != kNumberTypeFloat32) {
      MS_LOG(ERROR) << "param data type and tensor data type do not match.";
      return RET_ERROR;
    }
    inverse_ = true;
  } else {
    MS_LOG(ERROR) << "param data type not supported.";
    return RET_ERROR;
  }

  num_unit_ = static_cast<int>(in_tensor->DataSize());
  thread_n_num_ = MSMIN(thread_num_, num_unit_);
  thread_n_stride_ = UP_DIV(num_unit_, thread_n_num_);

  return RET_OK;
}

int QuantDTypeCastCPUKernel::ReSize() { return RET_OK; }

int QuantDTypeCastCPUKernel::QuantDTypeCast(int task_id) {
  int num_unit_thread = MSMIN(thread_n_stride_, num_unit_ - task_id * thread_n_stride_);
  if (num_unit_thread <= 0) {
    return RET_OK;
  }
  int thread_offset = task_id * thread_n_stride_;
  auto quant_arg = inputs_.front()->GetQuantParams().front();
  int ret;
  if (inverse_) {
    ret = DequantizeInt8(int8_ptr_ + thread_offset, float32_ptr_ + thread_offset, quant_arg.scale, quant_arg.zeroPoint,
                         num_unit_thread);
  } else {
    ret = QuantizeToInt8(float32_ptr_ + thread_offset, int8_ptr_ + thread_offset, quant_arg.scale,
                         quant_arg.zeroPoint, num_unit_thread);
  }
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "QuantDTypeCast error task_id[" << task_id << "] error_code[" << ret << "]";
    return RET_ERROR;
  }
  return RET_OK;
}

int QuantDTypeCastRun(int task_id, LiteParallelGroupEnv *penv, void *cdata) {
  auto g_kernel = reinterpret_cast<QuantDTypeCastCPUKernel *>(cdata);
  auto ret = g_kernel->QuantDTypeCast(task_id);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "QuantDTypeCastRun error task_id[" << task_id << "] error_code[" << ret << "]";
    return RET_ERROR;
  }
  return RET_OK;
}

int QuantDTypeCastCPUKernel::Run() {
  if (inverse_) {
    int8_ptr_ = reinterpret_cast<int8_t *>(inputs_[0]->Data());
    float32_ptr_ = reinterpret_cast<float *>(outputs_[0]->Data());
  } else {
    float32_ptr_ = reinterpret_cast<float *>(inputs_[0]->Data());
    int8_ptr_ = reinterpret_cast<int8_t *>(outputs_[0]->Data());
  }

  int ret = LiteBackendParallelLaunch(QuantDTypeCastRun, this, thread_n_num_);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Scale error error_code[" << ret << "]";
    return RET_ERROR;
  }

  return RET_OK;
}

kernel::LiteKernel *CpuQuantDTypeCastFp32KernelCreator(const std::vector<lite::tensor::Tensor *> &inputs,
                                                       const std::vector<lite::tensor::Tensor *> &outputs,
                                                       OpParameter *opParameter, const lite::Context *ctx,
                                                       const kernel::KernelKey &desc) {
  if (opParameter == nullptr) {
    MS_LOG(ERROR) << "Input opParameter is nullptr!";
    return nullptr;
  }
  auto *kernel = new (std::nothrow) QuantDTypeCastCPUKernel(opParameter, inputs, outputs, ctx);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "new QuantDTypeCastCPUKernel fail!";
    return nullptr;
  }
  auto ret = kernel->Init();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Init kernel failed! name: " << opParameter->name_ << ", type: "
                  << schema::EnumNamePrimitiveType(static_cast<schema::PrimitiveType>(opParameter->type_));
    delete kernel;
    return nullptr;
  }
  return kernel;
}

REG_KERNEL(kCPU, kNumberTypeInt8, PrimitiveType_QuantDTypeCast, CpuQuantDTypeCastFp32KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat32, PrimitiveType_QuantDTypeCast, CpuQuantDTypeCastFp32KernelCreator)
}  // namespace mindspore::kernel
