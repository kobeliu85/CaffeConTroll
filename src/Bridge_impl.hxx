//
//  Layer_impl.hxx
//  moka
//
//  Created by Ce Zhang on 1/13/15.
//  Copyright (c) 2015 Hazy Research. All rights reserved.
//

#ifndef moka_Bridge_impl_hxx
#define moka_Bridge_impl_hxx

template<typename DataType, NonLinearFunction FUNC>
Bridge<DataType, Layout_CRDB, DataType, Layout_CRDB, Bridge_CPU_CONV_LOWERINGTYPE1, FUNC>::
Bridge(InputLayerType * const _p_input_layer,
    OutputLayerType * const _p_output_layer):
i1R(_p_input_layer->p_data_cube->R), i1C(_p_input_layer->p_data_cube->C),
i1D(_p_input_layer->p_data_cube->D), i1B(_p_input_layer->p_data_cube->B),
i2R(_p_input_layer->p_model_cube->R), i2C(_p_input_layer->p_model_cube->C),
i2D(_p_input_layer->p_model_cube->D), i2B(_p_input_layer->p_model_cube->B),
oR(_p_output_layer->p_data_cube->R), oC(_p_output_layer->p_data_cube->C),
oD(_p_output_layer->p_data_cube->D), oB(_p_output_layer->p_data_cube->B),
p_input_layer(_p_input_layer), p_output_layer(_p_output_layer),
stepsize(_DEFAULT_STEPSIZE)
{
    report_forward_constructor.reset();
    report_forward_last_transfer.reset();
    report_forward_history.reset();
#ifdef _DO_ASSERT
    assert(oR==i1R-i2R+1); assert(oC==i1C-i2C+1);
    assert(i1D==i2D); assert(i1B==oB);
    assert(i2B==oD);
    assert(i2R==i2C);
#endif
    
    // first, allocate the space we need for lowering
    //   Following code is very messy without the Matrix interface -- TODO
    lconfig_forward.kernel_size = i2R;

    p_forward_lowered_data = new Cube<DataType, Layout_CRDB>(i2R*i2C*i2D, (i1R-i2R+1)*(i1C-i2C+1)*i1B ,1,1);
    
    Cube<DataType, Layout_CRDB> lowered_forward_model(p_input_layer->p_model_cube->p_data, i2B, i2R*i2C*i2D, 1, 1);
    
    Cube<DataType, Layout_CRDB> lowered_forward_output(p_output_layer->p_data_cube->p_data, i2B, (i1R-i2R+1)*(i1C-i2C+1)*i1B, 1, 1);
    
    std::cout << "Allocating " << (1.0*i2R*i2C*i2D*(i1R-i2R+1)*(i1C-i2C+1)*i1B*sizeof(DataType))/1024/1024/1024 << " GB data for the lowering matrix" << std::endl;
    
    p_forward_lower_connector = new Connector<DataType, Layout_CRDB, DataType, Layout_CRDB, Connector_Lowering_R1C1>(p_input_layer->p_data_cube, p_forward_lowered_data, &lconfig_forward);
    
    p_forward_gemm_kernel = new Kernel<DataType, Layout_CRDB, DataType, Layout_CRDB, DataType, Layout_CRDB, Kernel_GEMM_OpenBlas, KernelConfig_GEMM_NOTRANS_NOTRANS>(&lowered_forward_model, p_forward_lowered_data, &lowered_forward_output);
    
    p_forward_applyfunc_scanner = new Scanner<DataType, Layout_CRDB, FUNC>(p_output_layer->p_data_cube);
    
    // second, allocate the space we need for backward
    p_backward_outputgrad = new Cube<DataType, Layout_CRDB>(oR, oC, oD, oB);
    
    std::cout << "Allocating " << (1.0*i2R*i2C*i2D*(i1R-i2R+1)*(i1C-i2C+1)*i1B*sizeof(DataType))/1024/1024/1024 << " GB data for the lowering matrix" << std::endl;

    p_backward_inputgrad = new Cube<DataType, Layout_CRDB>(i2R*i2C*i2D, (i1R-i2R+1)*(i1C-i2C+1)*i1B ,1,1);
    
    p_backward_element_mul_kernel = new Kernel<DataType, Layout_CRDB, DataType, Layout_CRDB, DataType, Layout_CRDB, Kernel_ELEMENTWISEMUL_CPU, KernelConfig_TANHGRAD_ON_INPUT1>(p_output_layer->p_data_cube, p_output_layer->p_gradient_cube, p_backward_outputgrad);
    
    p_backward_gemm_updateweight_kernel = new Kernel<DataType, Layout_CRDB, DataType, Layout_CRDB, DataType, Layout_CRDB, Kernel_GEMM_OpenBlas, KernelConfig_GEMM_NOTRANS_TRANS>(&lowered_forward_output, p_forward_lowered_data, &lowered_forward_model);
    p_backward_gemm_updateweight_kernel->alpha = -stepsize;
    p_backward_gemm_updateweight_kernel->beta = 1.0;
    
    p_backward_gemm_updategrad_kernel = new Kernel<DataType_SFFloat, Layout_CRDB, DataType_SFFloat, Layout_CRDB, DataType_SFFloat, Layout_CRDB, Kernel_GEMM_OpenBlas, KernelConfig_GEMM_TRANS_NOTRANS>(&lowered_forward_model, &lowered_forward_output, p_backward_inputgrad);
    
    report_forward_constructor.end(0, 0, 0);
}

/**
 
 This function do the following.
 
 First Layer {iData, iModel, iGrad}
 Next Layer {oData, oModel, oGrad}
 
 Procedure:
 
     (1) iData -----lowering-----> LoweredData
 
     (2) LoweredData x iModel -----------> oData
 
     (3) oData -----non-linear func (if any)-----> oData
 
 **/
template<typename DataType, NonLinearFunction FUNC>
void Bridge<DataType, Layout_CRDB, DataType, Layout_CRDB, Bridge_CPU_CONV_LOWERINGTYPE1, FUNC>::
forward(){
    
    report_forward_last_transfer.reset();
    
    // (0) cast input model and output to matrix
    // This one should be refactored with the matrix interface
    Cube<DataType, Layout_CRDB> lowered_model(p_input_layer->p_model_cube->p_data, i2B, i2R*i2C*i2D, 1, 1);
    Cube<DataType, Layout_CRDB> lowered_output(p_output_layer->p_data_cube->p_data, i2B, (i1R-i2R+1)*(i1C-i2C+1)*i1B, 1, 1);
    
    // (1) do the lowering
    p_forward_lower_connector->transfer(p_input_layer->p_data_cube, p_forward_lowered_data);

    // (2) call GEMM kernel
    p_forward_gemm_kernel->compute(&lowered_model, p_forward_lowered_data, &lowered_output);
    
    // (3) apply non-linear functions
    if(FUNC != FUNC_NOFUNC){
        p_forward_applyfunc_scanner->apply(&lowered_output);
    }
    
    report_forward_last_transfer.end();
    report_forward_last_transfer.aggregate_onlystat(p_forward_gemm_kernel->report_last_transfer);
    report_forward_last_transfer.aggregate_onlystat(p_forward_lower_connector->report_last_transfer);
    
    if(FUNC != FUNC_NOFUNC){
        report_forward_last_transfer.aggregate_onlystat(p_forward_applyfunc_scanner->report_last_apply);
    }
    
    report_forward_history.aggregate(report_forward_last_transfer);
    
}


/**
 
 This function do the following.
 
 First Layer {iData, iModel, iGrad}
 Next Layer {oData, oModel, oGrad}
 
 Procedure:
 
 (1) oData element-wise-mul oGrad -------> BackPropogatedGradient
 
 (2) Update iGrad:
 
    (2.1) iModel x BackPropogatedGradient -----------> LoweredGradient_for_iData
 
    (2.2) LoweredGradient_for_iData ----inverse_of_lowering----> iGrad
 
 (3) BackPropogatedGradient x Lowered_iData * stepsize + iModel ---------> New iModel
 
 **/
template<typename DataType, NonLinearFunction FUNC>
void Bridge<DataType, Layout_CRDB, DataType, Layout_CRDB, Bridge_CPU_CONV_LOWERINGTYPE1, FUNC>::
backward(){
    
    report_backward_updateweight_last_transfer.reset();
    
    // (1) calculate the gradient of output and store in the buffer
    p_backward_element_mul_kernel->compute(p_output_layer->p_data_cube, p_output_layer->p_gradient_cube, p_backward_outputgrad);
    
    // (2) calculate the GEMM between the gradient of output and old kernel to calc the update on grad
    Cube<DataType, Layout_CRDB> lowered_model(p_input_layer->p_model_cube->p_data, i2B, i2R*i2C*i2D, 1, 1);
    Cube<DataType, Layout_CRDB> lowered_outputgrad(p_backward_outputgrad->p_data, i2B, (i1R-i2R+1)*(i1C-i2C+1)*i1B, 1, 1);
    
    //    - 2.1 GEMM between the gradient of output and old kernel
    p_backward_gemm_updategrad_kernel->compute(&lowered_model, &lowered_outputgrad, p_backward_inputgrad);
    
    //    - 2.2 undo the lowering (i.e., sum together all grad corresponding to the same unlowered position)
    p_forward_lower_connector->inverse_transfer(p_backward_inputgrad, p_input_layer->p_gradient_cube);
    
    // (3) calculate the GEMM between the gradient of output and lowered data to calc the update on kernel
    p_backward_gemm_updateweight_kernel->alpha = -stepsize;
    p_backward_gemm_updateweight_kernel->beta = 1.0;
    p_backward_gemm_updateweight_kernel->compute(&lowered_outputgrad, p_forward_lowered_data, &lowered_model);
    
    report_backward_updateweight_last_transfer.end();
    report_backward_updateweight_last_transfer.aggregate_onlystat(p_backward_element_mul_kernel->report_last_transfer);
    report_backward_updateweight_last_transfer.aggregate_onlystat(p_backward_gemm_updategrad_kernel->report_last_transfer);
    report_backward_updateweight_last_transfer.aggregate_onlystat(p_forward_lower_connector->report_last_transfer);
    report_backward_updateweight_last_transfer.aggregate_onlystat(p_backward_gemm_updateweight_kernel->report_last_transfer);
    
    report_backward_updateweight_history.aggregate(report_backward_updateweight_last_transfer);
    
}

#endif