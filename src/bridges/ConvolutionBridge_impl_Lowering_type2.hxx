//
//  ConvolutionBridge_Lowering2_impl.hxx
//  moka
//
//  Created by Firas Abuzaid on 2/16/15.
//  Copyright (c) 2015 Hazy Research. All rights reserved.
//

#ifndef moka_ConvolutionBridge_Lowering2_impl_hxx
#define moka_ConvolutionBridge_Lowering2_impl_hxx

// Constructor for convolution layer, lowering type 2
template <typename DataType, NonLinearFunction FUNC>
ConvolutionBridge<CPU_CONV_LOWERINGTYPE2, FUNC, DataType, Layout_CRDB, DataType, Layout_CRDB>::
ConvolutionBridge(InputLayerType * const _p_input_layer, OutputLayerType * const _p_output_layer,
  const cnn::LayerParameter * const _layer_param, const cnn::SolverParameter * const _solver_param)
: AbstractBridge<DataType, Layout_CRDB, DataType, Layout_CRDB>(_p_input_layer,
    _p_output_layer, _layer_param, _solver_param),
  K(layer_param->convolution_param().kernel_size()),
  // We are missing the abstraction of Logical Plan -- that is
  // why we cannot use layer_param here when there is grouping.
  // layer_param is the user input, not the Logical Plan
  num_output_features(_p_output_layer->dD),
  stride(layer_param->convolution_param().stride()),
  padding(layer_param->convolution_param().pad()),
  bias_term(layer_param->convolution_param().bias_term()),
  weight_filler(layer_param->convolution_param().weight_filler()),
  bias_filler(layer_param->convolution_param().bias_filler()) {

  report_forward_constructor.reset();
  report_forward_last_transfer.reset();
  report_forward_kernel.reset();
  report_forward_history.reset();
  report_forward_lowering.reset();
  report_backward_inverse_lowering.reset();
  report_backward_grad_kernel.reset();
  report_backward_weight_kernel.reset();

#ifdef _DO_ASSERT
  assert(oR == (iR + 2 * padding - K) / stride + 1);
  assert(oC == (iC + 2 * padding - K) / stride + 1);
  assert(iB == oB); assert(num_output_features == oD);
#endif

  p_model_cube = new LogicalCubeType(NULL, K, K, iD, num_output_features);

  p_model_cube_shadow = new LogicalCubeType(K, K, iD, num_output_features);
  initialize_logical_cube(p_model_cube_shadow, weight_filler);

  p_model_gradient_cube = new LogicalCubeType(K, K, iD, num_output_features);

  if (bias_term) {
    p_bias_cube = new LogicalCubeType(1, 1, num_output_features, 1);
    initialize_logical_cube(p_bias_cube, bias_filler);

    p_bias_gradient_cube = new LogicalCubeType(1, 1, num_output_features, 1);
  }

  p_forward_lowered_data = new LogicalCube<DataType, Layout_CRDB>(iD, iR*iC*iB,
      1, 1);
  p_forward_lowered_model = new LogicalCube<DataType, Layout_CRDB>(num_output_features*K*K,
      iD, 1, 1);

  LogicalCube<DataType, Layout_CRDB> lowered_forward_output(num_output_features*K*K, iR*iC*iB, 1, 1);

  p_forward_lower_model_connector = new Connector<DataType, Layout_CRDB, DataType, Layout_CRDB,
                            LOWERING_TYPE2>(p_model_cube_shadow, p_forward_lowered_model, K,
                                padding, stride);
  p_forward_lower_data_connector = new Connector<DataType, Layout_CRDB, DataType, Layout_CRDB,
                            LOWERING_TYPE2>(p_input_layer->p_data_cube, p_forward_lowered_data, K,
                                padding, stride);

  p_forward_gemm_kernel = new Kernel<DataType, Layout_CRDB, DataType, Layout_CRDB, DataType, Layout_CRDB,
                        Kernel_GEMM_OpenBlas, KernelConfig_GEMM_NOTRANS_NOTRANS>(p_forward_lowered_model,
                            p_forward_lowered_data, &lowered_forward_output);

  p_forward_applyfunc_scanner = new Scanner<DataType, Layout_CRDB, FUNC>(p_output_layer->p_data_cube);

  // second, allocate the space we need for backward
  // (only if we're applying a non-linear function
  // after the convolution)
  if (FUNC != FUNC_NOFUNC) {
    p_backward_outputgrad = new LogicalCube<DataType, Layout_CRDB>(oR, oC, oD, oB);
  }

  p_backward_inputgrad = new LogicalCube<DataType, Layout_CRDB>(iD, iR*iC*iB, 1, 1);

  // TODO: figure out a better way to support other functions besides tanh
  if (FUNC != FUNC_NOFUNC) {
    p_backward_element_mul_kernel = new Kernel<DataType, Layout_CRDB, DataType, Layout_CRDB, DataType,
                                  Layout_CRDB, Kernel_ELEMENTWISEMUL_CPU, KernelConfig_TANHGRAD_ON_INPUT1>();
  }

  p_backward_gemm_updateweight_kernel = new Kernel<DataType, Layout_CRDB, DataType, Layout_CRDB, DataType,
                                      Layout_CRDB, Kernel_GEMM_OpenBlas,
                                      KernelConfig_GEMM_NOTRANS_TRANS>(&lowered_forward_output,
                                          p_forward_lowered_data, p_forward_lowered_model);

   p_backward_gemm_updategrad_kernel = new Kernel<DataType_SFFloat, Layout_CRDB, DataType_SFFloat, Layout_CRDB,
                                     DataType_SFFloat, Layout_CRDB, Kernel_GEMM_OpenBlas,
                                     KernelConfig_GEMM_TRANS_NOTRANS>(p_forward_lowered_model,
                                         &lowered_forward_output, p_backward_inputgrad);

  report_forward_constructor.end(0, 0, 0);
}

// Intiailize a Logical Cube using a FillerParameter. This is only called if layer_param is
// non-NULL.
template <typename DataType, NonLinearFunction FUNC>
void ConvolutionBridge<CPU_CONV_LOWERINGTYPE2, FUNC, DataType, Layout_CRDB, DataType, Layout_CRDB>::
initialize_logical_cube(const LogicalCubeType * cube, const cnn::FillerParameter filler_param) {
  const string type = filler_param.type();
  if (type == "constant") {
    Util::constant_initialize<DataType>(cube->get_p_data(), (DataType) filler_param.value(), cube->n_elements);
  } else if (type == "xavier") {
    Util::xavier_initialize(cube->get_p_data(), cube->n_elements, cube->B);
  } else if (type == "bernoulli") {
    Util::bernoulli_initialize(cube->get_p_data(), cube->n_elements, filler_param.value());
  } else if (type == "gaussian") {
    Util::gaussian_initialize(cube->get_p_data(), cube->n_elements, (DataType) filler_param.mean(),
        (DataType) filler_param.std());
  } else {
    cout << "ERROR! INITIALIZATION TYPE NOT SUPPORTED!" << endl;
    assert(false);
  }
}

template <typename T>
void aggregate(LogicalCube<T, Layout_CRDB> * const in, LogicalCube<T, Layout_CRDB> * const out, const int K,
    const int N, const int oR, const int oC, const int iB, const int stride, const int padding) {
  assert(padding == 0); // TODO: support padding > 1
  for (int b = 0; b < iB; ++b) {
    const int start_x_b = b*N*N;
    const int out_x_base = b*oR*oC;
    for (int start_y_base = 0; start_y_base < in->R; start_y_base += K*K) {
      const int out_y = start_y_base / (K*K);
      for (int y = 0; y < oR; y++) {
        for (int x = 0; x < oC; x++) {
          const int start_x_base = start_x_b + (y*N + x)*stride;
          const int out_x = out_x_base + y*oC + x;
          for (int i = 0, start_x = start_x_base, start_y = start_y_base; i < K;
              start_y += K, start_x += N, ++i) {
            for (int p = 0; p < K; p++) {
              int in_y = start_y + p;
              int in_x = start_x + p;
              *out->logical_get(out_y, out_x, 0, 0) +=
                *in->logical_get(in_y, in_x, 0, 0);
            }
          }
        }
      }
    }
  }
}

template <typename T>
void disaggregate(LogicalCube<T, Layout_CRDB> * const out, LogicalCube<T, Layout_CRDB> * const in, const int K,
    const int N, const int oR, const int oC, const int iB, const int stride, const int padding) {
  for (int b = 0; b < iB; ++b) {
    const int start_x_b = b*N*N;
    const int out_x_base = b*oR*oC;
    for (int start_y_base = 0; start_y_base < in->R; start_y_base += K*K) {
      const int out_y = start_y_base / (K*K);
      for (int y = 0; y < oR; y++) {
        for (int x = 0; x < oC; x++) {
          const int start_x_base = start_x_b + (y*N + x)*stride;
          const int out_x = out_x_base + y*oC + x;
          for (int i = 0, start_x = start_x_base, start_y = start_y_base; i < K;
              start_y += K, start_x += N, ++i) {
            for (int p = 0; p < K; p++) {
              int in_y = start_y + p;
              int in_x = start_x + p;
	      *in->logical_get(in_y, in_x, 0, 0) =
		*out->logical_get(out_y, out_x, 0, 0);
            }
          }
        }
      }
    }
  }
}

/**
 * This function does the following:
 *
 * First Layer {iData, iModel, iGrad}
 * Next Layer {oData, oModel, oGrad}
 *
 * Procedure:
 *
 * (1) iData -----lowering-----> LoweredData
 *
 * (2) LoweredData x iModel -----------> oData
 *
 * (3) oData -----non-linear func (if any)-----> oData
 *
 **/
template <typename DataType, NonLinearFunction FUNC>
void ConvolutionBridge<CPU_CONV_LOWERINGTYPE2, FUNC, DataType, Layout_CRDB, DataType, Layout_CRDB>::
forward() {

  Util::set_num_threads(run_with_n_threads);

  report_forward_last_transfer.reset();

  // (0) cast input model and output to matrix
  // This one should be refactored with the matrix interface
  LogicalCube<DataType, Layout_CRDB> lowered_gemm_output(num_output_features*K*K, iR*iC*iB, 1, 1);
  LogicalCube<DataType, Layout_CRDB> lowered_output(p_output_layer->p_data_cube->get_p_data(),
      num_output_features, oR*oC*iB, 1, 1);

  // (1) do the lowering. For Lowering Type 2, we lower the model and the data
  p_forward_lower_model_connector->lower_model_cube(p_model_cube, p_forward_lowered_model);
  p_forward_lower_data_connector->lower_data_cube(p_input_layer->p_data_cube, p_forward_lowered_data);

    // (2) call GEMM kernel
  p_forward_gemm_kernel->compute(p_forward_lowered_model, p_forward_lowered_data, &lowered_gemm_output);

  // (3) perform convolution on lowered_gemm_output to get lowered_output
  lowered_output.reset_cube();
  assert(iR == iC);
  aggregate(&lowered_gemm_output, &lowered_output, K, iR, oR, oC, iB, stride, padding);

  // (3) apply non-linear functions
  if (FUNC != FUNC_NOFUNC) {
     p_forward_applyfunc_scanner->apply(&lowered_output);
  }

  // Right now the output we get is of the form:
  // [(b_0, d_0), (b_1, d_0), ... , (b_n, d_0)
  //
  //  (b_0, d_m), (b_1, d_m), ... , (b_n, d_m)]
  //  we need to transpose this, so that the outputs
  //  of a single batch are contiguous in memory.
  //  For now, we will call remap_output to fix this
  //  issue.
  //
  //  TODO: figure out how to properly transpose the
  //  inputs so that we get the correct output without
  //  needing to call remap
  p_output_layer->p_data_cube->template remap_output<LOWERING_TYPE2>(num_output_features, iB, oR*oC);

  // add bias
  if (bias_term) {
    const size_t output_feature_size = oR*oC;
    const DataType * const bias_data = p_bias_cube->get_p_data();
    for (size_t o_b = 0; o_b < oB; ++o_b) {
      for (size_t o_d = 0; o_d < oD; ++o_d) {
        const LogicalMatrix<DataType> output_data_slice =
          p_output_layer->p_data_cube->get_logical_matrix(o_d, o_b);
        const DataType bias = bias_data[o_d];
        for (size_t i = 0; i < output_feature_size; ++i) {
          output_data_slice.p_data[i] += bias;
        }
      }
    }
  }

  report_forward_last_transfer.end();
  report_forward_last_transfer.aggregate_onlystat(p_forward_gemm_kernel->report_last_lowering);
  report_forward_last_transfer.aggregate_onlystat(p_forward_lower_model_connector->report_last_lowering);
  report_forward_last_transfer.aggregate_onlystat(p_forward_lower_data_connector->report_last_lowering);

  if (FUNC != FUNC_NOFUNC) {
    report_forward_last_transfer.aggregate_onlystat(p_forward_applyfunc_scanner->report_last_apply);
  }

  report_forward_history.aggregate(report_forward_last_transfer);
  report_forward_kernel.aggregate(p_forward_gemm_kernel->report_last_lowering);
  report_forward_lowering.aggregate(p_forward_lower_model_connector->report_last_lowering);
}

/**
  * This function does the following:
  *
  * First Layer {iData, iModel, iGrad}
  * Next Layer {oData, oModel, oGrad}
  *
  * Procedure:
  *
  * (1) oData element-wise-mul oGrad -------> BackPropogatedGradient
  *
  * (2) Update iGrad:
  *
  * (2.1) iModel x BackPropogatedGradient -----------> LoweredGradient_for_iData
  *
  * (2.2) LoweredGradient_for_iData ----inverse_of_lowering----> iGrad
  *
  * (3) BackPropogatedGradient x Lowered_iData * stepsize + iModel ---------> New iModel
  *
 **/
template <typename DataType, NonLinearFunction FUNC>
void ConvolutionBridge<CPU_CONV_LOWERINGTYPE2, FUNC, DataType, Layout_CRDB, DataType, Layout_CRDB>::
backward() {

  Util::set_num_threads(run_with_n_threads);

  report_backward_updateweight_last_transfer.reset();

  // (1) calculate the gradient of output and store in the buffer
  if (FUNC != FUNC_NOFUNC) {
    p_backward_element_mul_kernel->compute(p_output_layer->p_data_cube, p_output_layer->p_gradient_cube,
        p_backward_outputgrad);
  } else {
    p_backward_outputgrad = p_output_layer->p_gradient_cube;
  }

  // Here, we again call remap_output, but we do so BEFORE anything else
  p_backward_outputgrad->template remap_output<LOWERING_TYPE2>(oB, num_output_features, oR*oC);

  // (2) disaggregate (inverse of aggregate) the gradient output
  LogicalCube<DataType, Layout_CRDB> aggregated_lowered_output_grad(p_backward_outputgrad->get_p_data(),
      num_output_features, oR*oC*iB, 1, 1);

  LogicalCube<DataType, Layout_CRDB> lowered_output_grad(num_output_features*K*K, iR*iC*iB, 1, 1);
  lowered_output_grad.reset_cube();
  disaggregate(&aggregated_lowered_output_grad, &lowered_output_grad, K, iR, oR, oC, iB, stride, padding);

  // (3) update the bias term, summing over the gradients for each O and B
  if (bias_term) {
    const size_t output_feature_size = oR*oC;
    p_bias_gradient_cube->reset_cube();
    DataType * const bias_data = p_bias_gradient_cube->get_p_data();
    for (size_t o_b = 0; o_b < oB; ++o_b) {
      for (size_t o_d = 0; o_d < oD; ++o_d) {
        const LogicalMatrix<DataType> input_grad_slice = p_output_layer->p_gradient_cube->get_logical_matrix(o_d, o_b);
        DataType sum = DataType(0.0);
        for (size_t i = 0; i < output_feature_size; ++i) {
          sum += input_grad_slice.p_data[i];
        }
        bias_data[o_d] += sum;
      }
    }
  }

  if (needs_to_calc_backward_grad) {
    // - 2.1 GEMM between the gradient of output and old kernel
    p_backward_gemm_updategrad_kernel->compute(p_forward_lowered_model, &lowered_output_grad, p_backward_inputgrad);
    // - 2.2 undo the lowering (i.e., sum together all grad corresponding to the same unlowered position)
    p_forward_lower_data_connector->inverse_lower_data_cube(p_backward_inputgrad, p_input_layer->p_gradient_cube);
  }

  LogicalCube<DataType, Layout_CRDB> lowered_model_grad(num_output_features, K*K*iD, 1, 1);
  // (4) calculate the GEMM between the gradient of output and lowered data to calc the update on kernel
  p_backward_gemm_updateweight_kernel->alpha = 1.0;
  p_backward_gemm_updateweight_kernel->beta = 0.0;
  p_backward_gemm_updateweight_kernel->compute(&lowered_output_grad, p_forward_lowered_data, &lowered_model_grad);

  p_forward_lower_model_connector->inverse_lower_model_cube(p_model_gradient_cube, &lowered_model_grad);

  report_backward_updateweight_last_transfer.end();

  if (FUNC != FUNC_NOFUNC) {
    report_backward_updateweight_last_transfer.aggregate_onlystat(p_backward_element_mul_kernel->report_last_lowering);
  }

  report_backward_updateweight_last_transfer.aggregate_onlystat(p_backward_gemm_updategrad_kernel->report_last_lowering);
  report_backward_updateweight_last_transfer.aggregate_onlystat(p_forward_lower_model_connector->report_last_inverse_lowering);
  report_backward_updateweight_last_transfer.aggregate_onlystat(p_backward_gemm_updateweight_kernel->report_last_lowering);

  report_backward_inverse_lowering.aggregate(p_forward_lower_model_connector->report_last_inverse_lowering);
  report_backward_weight_kernel.aggregate(p_backward_gemm_updateweight_kernel->report_last_lowering);
  report_backward_grad_kernel.aggregate(p_backward_gemm_updategrad_kernel->report_last_lowering);
  report_backward_updateweight_history.aggregate(report_backward_updateweight_last_transfer);
}

template <typename DataType, NonLinearFunction FUNC>
ConvolutionBridge<CPU_CONV_LOWERINGTYPE2, FUNC, DataType, Layout_CRDB, DataType, Layout_CRDB>::
~ConvolutionBridge() {
  if (FUNC != FUNC_NOFUNC) {
    delete p_backward_element_mul_kernel;
    delete p_backward_outputgrad;
  }
  if (bias_term) {
    delete p_bias_cube;
  }
  delete p_model_cube; delete p_forward_lowered_model; delete p_forward_lowered_data;
  delete p_backward_gemm_updategrad_kernel; delete p_backward_gemm_updateweight_kernel;
  delete p_backward_inputgrad; delete p_forward_gemm_kernel;
  delete p_forward_lower_model_connector;
}

#endif