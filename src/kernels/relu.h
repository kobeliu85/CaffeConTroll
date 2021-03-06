
#ifndef _KERNEL_RELU_H
#define _KERNEL_RELU_H

#include "../sched/DeviceHeader.h"

struct _relu_backward_arg_helper {
  char * input_data;
};

#ifdef _GPU_TARGET
__host__ __device__
#endif
size_t _f_src_to_dst_relu_forward(size_t src_pos, void * const _arg);

#ifdef _GPU_TARGET
__host__ __device__
#endif
void _f_relu_forward(void * input, void * output, void * const _arg,
    const size_t dst_index);

#ifdef _GPU_TARGET
__host__ __device__
#endif
size_t _f_src_to_dst_relu_backward(size_t src_pos, void * const _arg);

#ifdef _GPU_TARGET
__host__ __device__
#endif
void _f_relu_backward(void * input, void * output, void * const _arg,
    const size_t dst_index);

#endif
