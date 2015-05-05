//
//  AbstractBridge.h
//  moka
//
//  Created by Firas Abuzaid on 1/22/15.
//  Copyright (c) 2015 Hazy Research. All rights reserved.
//

#ifndef moka_Abstract_Bridge_h
#define moka_Abstract_Bridge_h

#include "../LogicalCube.h"
#include "../Connector.h"
#include "../Kernel.h"
#include "../Report.h"
#include "../Scanner.h"
#include "PhysicalOperator.h"
#include "../Layer.h"
#include "../parser/cnn.pb.h"
#include "../algorithms/GradientUpdater.h"

#include "../sched/DeviceDriver_CPU.h"
#ifdef _INCLUDE_GPUDRIVER
#include "../sched/DeviceDriver_GPU.h"
#endif

template
<typename InputLayerDataType, LayoutType InputLayerLayout,
  typename OutputLayerDataType, LayoutType OutputLayerLayout, typename DriverClass>
class AbstractBridge : public PhysicalOperator {
  protected:
    // SHADJIS TODO: curr_B is only used in parallelized bridge now,
    // but I think it should be used everywhere to handle the last batch
    size_t curr_B;
    LogicalCube<InputLayerDataType, InputLayerLayout> * input_d_cube;
    LogicalCube<InputLayerDataType, InputLayerLayout> * input_g_cube;
    LogicalCube<OutputLayerDataType, OutputLayerLayout> * output_d_cube;
    LogicalCube<OutputLayerDataType, OutputLayerLayout> * output_g_cube;

  public:
    std::string name; // lets give Bridge a name

    typedef Layer<InputLayerDataType, InputLayerLayout> InputLayerType;
    typedef Layer<OutputLayerDataType, OutputLayerLayout> OutputLayerType;

    const size_t iR, iC, iD, iB; // Size of the input data, LogicalCube 1
    const size_t oR, oC, oD, oB; // Size of the output data, LogicalCube 2

    InputLayerType * const p_input_layer;
    OutputLayerType * const p_output_layer;

    const cnn::LayerParameter * const layer_param;
    const cnn::SolverParameter * const solver_param;

    DriverClass * const p_driver;

    bool needs_to_calc_backward_grad;

    Report report_constructor;
    Report report_last_lowering;
    Report report_history;

    bool bias_term;

    void report_forward() {
        std::cout << std::endl;
        std::cout << "## FORWARD REPORT OF LAYER " << name << " ##" << std::endl;
        report_forward_last_transfer.print();
    }

    void report_backward() {
        std::cout << std::endl;
        std::cout << "## BACKWARD REPORT OF LAYER " << name << " ##" << std::endl;
        report_backward_updateweight_last_transfer.print();
    }

    void copy_from_local_to_device(LogicalCube<InputLayerDataType, InputLayerLayout> * const dst,
	LogicalCube<InputLayerDataType, InputLayerLayout> * const src) {
        // We know local is a CPU driver
        CPUDriver *local_cpu_driver = new CPUDriver();
        p_driver->memcpy(dst->get_device_pointer(p_driver), src->get_device_pointer(local_cpu_driver));
        delete local_cpu_driver;
    }

    void copy_from_device_to_local(LogicalCube<InputLayerDataType, InputLayerLayout> * const dst,
	LogicalCube<InputLayerDataType, InputLayerLayout> * const src) {
        // We know local is a CPU driver
        CPUDriver *local_cpu_driver = new CPUDriver();
        p_driver->memcpy(dst->get_device_pointer(local_cpu_driver), src->get_device_pointer(p_driver));
        delete local_cpu_driver;
    }

    // Bridges which subclass AbstractBridge may override these four methods later
    // (e.g. ConvolutionBridge). Most, however, won't, since only ConvolutionBridge
    // and FullyConnected Bridge have weights that need to be updated
    virtual void set_model_cube(LogicalCube<InputLayerDataType, InputLayerLayout> * model) {}

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_model_cube() {
        return NULL;
    }

    virtual void set_bias_cube(LogicalCube<InputLayerDataType, InputLayerLayout> * bias) {}

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_bias_cube() {
        return NULL;
    }

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_model_grad_cube() {
        return NULL;
    }

    LogicalCube<InputLayerDataType, InputLayerLayout> * const get_bias_grad_cube() {
        return NULL;
    }

    // Need these for snapshot tests
    virtual GradientUpdater<InputLayerDataType, CPUDriver> * const get_model_updater() {
        return NULL;
    }

    virtual GradientUpdater<InputLayerDataType, CPUDriver> * const get_bias_updater() {
        return NULL;
    }

    virtual void set_curr_batch_size(const size_t _curr_B) {
      curr_B = _curr_B;
    }

    // First constructor, which takes in a cnn::LayerParameter as a third argument. This will
    // be used when initializing from a *.prototxt file
    AbstractBridge<InputLayerDataType, InputLayerLayout, OutputLayerDataType,
      OutputLayerLayout, DriverClass>(InputLayerType * const _p_input_layer,
          OutputLayerType * const _p_output_layer, const cnn::LayerParameter * const _layer_param,
          const cnn::SolverParameter * const _solver_param, DriverClass * const _p_driver) :
        curr_B(_p_input_layer->p_data_cube->B), iR(_p_input_layer->p_data_cube->R),
        iC(_p_input_layer->p_data_cube->C), iD(_p_input_layer->p_data_cube->D),
        iB(_p_input_layer->p_data_cube->B), oR(_p_output_layer->p_data_cube->R),
        oC(_p_output_layer->p_data_cube->C), oD(_p_output_layer->p_data_cube->D),
        oB(_p_output_layer->p_data_cube->B), p_input_layer(_p_input_layer),
        p_output_layer(_p_output_layer), layer_param(_layer_param),
        solver_param(_solver_param), p_driver(_p_driver), bias_term(false) {

          // Default non-softmax: Use constructor to own data. Allocates on the device.
          input_d_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(iR, iC, iD, iB, p_driver);
          input_g_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(iR, iC, iD, iB, p_driver);
          output_d_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(oR, oC, oD, oB, p_driver);
          output_g_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(oR, oC, oD, oB, p_driver);
        }

    // Second constructor, which does NOT take in a cnn::LayerParameter as a third argument.
    // (Used only for Softmax)
    AbstractBridge<InputLayerDataType, InputLayerLayout, OutputLayerDataType,
      OutputLayerLayout, DriverClass>(InputLayerType * const _p_input_layer,
          OutputLayerType * const _p_output_layer, DriverClass * const _p_driver) :
        curr_B(_p_input_layer->p_data_cube->B),
        iR(_p_input_layer->p_data_cube->R), iC(_p_input_layer->p_data_cube->C),
        iD(_p_input_layer->p_data_cube->D), iB(_p_input_layer->p_data_cube->B),
        oR(_p_output_layer->p_data_cube->R), oC(_p_output_layer->p_data_cube->C),
        oD(_p_output_layer->p_data_cube->D), oB(_p_output_layer->p_data_cube->B),
        p_input_layer(_p_input_layer), p_output_layer(_p_output_layer),
        layer_param(NULL), solver_param(NULL), p_driver(_p_driver),
        bias_term(false) {

          // Default softmax: Use constructor to own data. Allocates on the device.
          input_d_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(iR, iC, iD, iB, p_driver);
          input_g_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(iR, iC, iD, iB, p_driver);
          output_d_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(oR, oC, oD, oB, p_driver);
          output_g_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(oR, oC, oD, oB, p_driver);
        }

    // This needs to be virtual, so we can delete the subclass bridges
    virtual ~AbstractBridge() {
      delete input_d_cube;  input_d_cube  = NULL;
      delete input_g_cube;  input_g_cube  = NULL;
      delete output_d_cube; output_d_cube = NULL;
      delete output_g_cube; output_g_cube = NULL;
    }
};

// CPUDriver specialization
template
<typename InputLayerDataType, LayoutType InputLayerLayout,
  typename OutputLayerDataType, LayoutType OutputLayerLayout>
class AbstractBridge<InputLayerDataType, InputLayerLayout, OutputLayerDataType,
      OutputLayerLayout, CPUDriver> : public PhysicalOperator {
  protected:
    // the size of the current training batch
    // always <= iB
    size_t curr_B;
    LogicalCube<InputLayerDataType, InputLayerLayout> * input_d_cube;
    LogicalCube<InputLayerDataType, InputLayerLayout> * input_g_cube;
    LogicalCube<OutputLayerDataType, OutputLayerLayout> * output_d_cube;
    LogicalCube<OutputLayerDataType, OutputLayerLayout> * output_g_cube;

  public:
    std::string name; // lets give Bridge a name

    typedef Layer<InputLayerDataType, InputLayerLayout> InputLayerType;
    typedef Layer<OutputLayerDataType, OutputLayerLayout> OutputLayerType;

    const size_t iR, iC, iD, iB; // Size of the input data, LogicalCube 1
    const size_t oR, oC, oD, oB; // Size of the output data, LogicalCube 2

    InputLayerType * const p_input_layer;
    OutputLayerType * const p_output_layer;

    const cnn::LayerParameter * const layer_param;
    const cnn::SolverParameter * const solver_param;

    CPUDriver * const p_driver;

    bool needs_to_calc_backward_grad;

    Report report_constructor;
    Report report_last_lowering;
    Report report_history;

    bool bias_term;

    void report_forward() {
        std::cout << std::endl;
        std::cout << "## FORWARD REPORT OF LAYER " << name << " ##" << std::endl;
        report_forward_last_transfer.print();
    }

    void report_backward() {
        std::cout << std::endl;
        std::cout << "## BACKWARD REPORT OF LAYER " << name << " ##" << std::endl;
        report_backward_updateweight_last_transfer.print();
    }

    // If p_driver == CPUDriver, then we just need to reassign pointers
    void copy_from_local_to_device(LogicalCube<InputLayerDataType, InputLayerLayout> * const dst,
	LogicalCube<InputLayerDataType, InputLayerLayout> * const src) {
      dst->set_p_data(src->get_p_data());
    }

    void copy_from_device_to_local(LogicalCube<InputLayerDataType, InputLayerLayout> * const dst,
	LogicalCube<InputLayerDataType, InputLayerLayout> * const src) {
      dst->set_p_data(src->get_p_data());
    }

    // Bridges which subclass AbstractBridge may override these four methods later
    // (e.g. ConvolutionBridge). Most, however, won't, since only ConvolutionBridge
    // and FullyConnected Bridge have weights that need to be updated
    virtual void set_model_cube(LogicalCube<InputLayerDataType, InputLayerLayout> * model) {}

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_model_cube() {
        return NULL;
    }

    virtual void set_bias_cube(LogicalCube<InputLayerDataType, InputLayerLayout> * bias) {}

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_bias_cube() {
        return NULL;
    }

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_model_grad_cube() {
        return NULL;
    }

    LogicalCube<InputLayerDataType, InputLayerLayout> * const get_bias_grad_cube() {
        return NULL;
    }

    // Need these for snapshot tests
    virtual GradientUpdater<InputLayerDataType, CPUDriver> * const get_model_updater() {
        return NULL;
    }

    virtual GradientUpdater<InputLayerDataType, CPUDriver> * const get_bias_updater() {
        return NULL;
    }

    void set_curr_batch_size(const size_t _curr_B) {
      curr_B = _curr_B;
    }

    // First constructor, which takes in a cnn::LayerParameter as a third argument. This will
    // be used when initializing from a *.prototxt file
    AbstractBridge<InputLayerDataType, InputLayerLayout, OutputLayerDataType,
      OutputLayerLayout, CPUDriver>(InputLayerType * const _p_input_layer,
          OutputLayerType * const _p_output_layer, const cnn::LayerParameter * const _layer_param,
          const cnn::SolverParameter * const _solver_param, CPUDriver * const _p_driver) :
        curr_B(_p_input_layer->p_data_cube->B), iR(_p_input_layer->p_data_cube->R),
        iC(_p_input_layer->p_data_cube->C), iD(_p_input_layer->p_data_cube->D),
        iB(_p_input_layer->p_data_cube->B), oR(_p_output_layer->p_data_cube->R),
        oC(_p_output_layer->p_data_cube->C), oD(_p_output_layer->p_data_cube->D),
        oB(_p_output_layer->p_data_cube->B), p_input_layer(_p_input_layer),
        p_output_layer(_p_output_layer), layer_param(_layer_param),
        solver_param(_solver_param), p_driver(_p_driver), bias_term(false) {

          // CPU: Use constructor to not own data. Does not allocate on the device.
          input_d_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(NULL, iR, iC, iD, iB, p_driver);
          input_g_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(NULL, iR, iC, iD, iB, p_driver);
          output_d_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(NULL, oR, oC, oD, oB, p_driver);
          output_g_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(NULL, oR, oC, oD, oB, p_driver);
        }

    // Second constructor, which does NOT take in a cnn::LayerParameter as a third argument.
    // (Used only for Softmax)
    AbstractBridge<InputLayerDataType, InputLayerLayout, OutputLayerDataType,
      OutputLayerLayout, CPUDriver>(InputLayerType * const _p_input_layer,
          OutputLayerType * const _p_output_layer, CPUDriver * const _p_driver) :
        curr_B(_p_input_layer->p_data_cube->B),
        iR(_p_input_layer->p_data_cube->R), iC(_p_input_layer->p_data_cube->C),
        iD(_p_input_layer->p_data_cube->D), iB(_p_input_layer->p_data_cube->B),
        oR(_p_output_layer->p_data_cube->R), oC(_p_output_layer->p_data_cube->C),
        oD(_p_output_layer->p_data_cube->D), oB(_p_output_layer->p_data_cube->B),
        p_input_layer(_p_input_layer), p_output_layer(_p_output_layer),
        layer_param(NULL), solver_param(NULL), p_driver(_p_driver),
        bias_term(false) {

          // CPU: Use constructor to not own data. Does not allocate on the device.
          input_d_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(NULL, iR, iC, iD, iB, p_driver);
          input_g_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(NULL, iR, iC, iD, iB, p_driver);
          output_d_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(NULL, oR, oC, oD, oB, p_driver);
          output_g_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(NULL, oR, oC, oD, oB, p_driver);
        }

    // This needs to be virtual, so we can delete the subclass bridges
    virtual ~AbstractBridge() {
      delete input_d_cube;  input_d_cube  = NULL;
      delete input_g_cube;  input_g_cube  = NULL;
      delete output_d_cube; output_d_cube = NULL;
      delete output_g_cube; output_g_cube = NULL;
    }
};


#ifdef _INCLUDE_GPUDRIVER
// GPUDriver specialization
template
<typename InputLayerDataType, LayoutType InputLayerLayout,
  typename OutputLayerDataType, LayoutType OutputLayerLayout>
class AbstractBridge<InputLayerDataType, InputLayerLayout, OutputLayerDataType,
      OutputLayerLayout, GPUDriver> : public PhysicalOperator {
  protected:
    // the size of the current training batch
    // always <= iB
    size_t curr_B;
    LogicalCube<InputLayerDataType, InputLayerLayout> * input_d_cube;
    LogicalCube<InputLayerDataType, InputLayerLayout> * input_g_cube;
    LogicalCube<OutputLayerDataType, OutputLayerLayout> * output_d_cube;
    LogicalCube<OutputLayerDataType, OutputLayerLayout> * output_g_cube;

  public:
    std::string name; // lets give Bridge a name

    typedef Layer<InputLayerDataType, InputLayerLayout> InputLayerType;
    typedef Layer<OutputLayerDataType, OutputLayerLayout> OutputLayerType;

    const size_t iR, iC, iD, iB; // Size of the input data, LogicalCube 1
    const size_t oR, oC, oD, oB; // Size of the output data, LogicalCube 2

    InputLayerType * const p_input_layer;
    OutputLayerType * const p_output_layer;

    const cnn::LayerParameter * const layer_param;
    const cnn::SolverParameter * const solver_param;

    GPUDriver * const p_driver;

    bool needs_to_calc_backward_grad;

    Report report_constructor;
    Report report_last_lowering;
    Report report_history;

    bool bias_term;

    void report_forward() {
        std::cout << std::endl;
        std::cout << "## FORWARD REPORT OF LAYER " << name << " ##" << std::endl;
        report_forward_last_transfer.print();
    }

    void report_backward() {
        std::cout << std::endl;
        std::cout << "## BACKWARD REPORT OF LAYER " << name << " ##" << std::endl;
        report_backward_updateweight_last_transfer.print();
    }

    void copy_from_local_to_device(LogicalCube<InputLayerDataType, InputLayerLayout> * const dst,
	LogicalCube<InputLayerDataType, InputLayerLayout> * const src) {
        // We know local is a CPU driver
        CPUDriver *local_cpu_driver = new CPUDriver();
        p_driver->memcpy(dst->get_device_pointer(p_driver), src->get_device_pointer(local_cpu_driver));
        delete local_cpu_driver;
    }

    void copy_from_device_to_local(LogicalCube<InputLayerDataType, InputLayerLayout> * const dst,
	LogicalCube<InputLayerDataType, InputLayerLayout> * const src) {
        // We know local is a CPU driver
        CPUDriver *local_cpu_driver = new CPUDriver();
        p_driver->memcpy(dst->get_device_pointer(local_cpu_driver), src->get_device_pointer(p_driver));
        delete local_cpu_driver;
    }

    // Bridges which subclass AbstractBridge may override these four methods later
    // (e.g. ConvolutionBridge). Most, however, won't, since only ConvolutionBridge
    // and FullyConnected Bridge have weights that need to be updated
    virtual void set_model_cube(LogicalCube<InputLayerDataType, InputLayerLayout> * model) {}

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_model_cube() {
        return NULL;
    }

    virtual void set_bias_cube(LogicalCube<InputLayerDataType, InputLayerLayout> * bias) {}

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_bias_cube() {
        return NULL;
    }

    virtual LogicalCube<InputLayerDataType, InputLayerLayout> * const get_model_grad_cube() {
        return NULL;
    }

    LogicalCube<InputLayerDataType, InputLayerLayout> * const get_bias_grad_cube() {
        return NULL;
    }

    // Need these for snapshot tests
    virtual GradientUpdater<InputLayerDataType, GPUDriver> * const get_model_updater() {
        return NULL;
    }

    virtual GradientUpdater<InputLayerDataType, GPUDriver> * const get_bias_updater() {
        return NULL;
    }

    void set_curr_batch_size(const size_t _curr_B) {
      curr_B = _curr_B;
    }

    // First constructor, which takes in a cnn::LayerParameter as a third argument. This will
    // be used when initializing from a *.prototxt file
    AbstractBridge<InputLayerDataType, InputLayerLayout, OutputLayerDataType,
      OutputLayerLayout, GPUDriver>(InputLayerType * const _p_input_layer,
          OutputLayerType * const _p_output_layer, const cnn::LayerParameter * const _layer_param,
          const cnn::SolverParameter * const _solver_param, GPUDriver * const _p_driver) :
        curr_B(_p_input_layer->p_data_cube->B), iR(_p_input_layer->p_data_cube->R),
        iC(_p_input_layer->p_data_cube->C), iD(_p_input_layer->p_data_cube->D),
        iB(_p_input_layer->p_data_cube->B), oR(_p_output_layer->p_data_cube->R),
        oC(_p_output_layer->p_data_cube->C), oD(_p_output_layer->p_data_cube->D),
        oB(_p_output_layer->p_data_cube->B), p_input_layer(_p_input_layer),
        p_output_layer(_p_output_layer), layer_param(_layer_param),
        solver_param(_solver_param), p_driver(_p_driver), bias_term(false) {

          // GPU: Use constructor to own data. I.e. allocate this on the device.
          // SHADJIS TODO: For GPU, if we decide to copy 1 image at a time in the
          // batch to the GPU, iB / oB below would change to 1. 
          input_d_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(iR, iC, iD, iB, p_driver);
          input_g_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(iR, iC, iD, iB, p_driver);
          output_d_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(oR, oC, oD, oB, p_driver);
          output_g_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(oR, oC, oD, oB, p_driver);
        }

    // Second constructor, which does NOT take in a cnn::LayerParameter as a third argument.
    // (Used only for Softmax)
    AbstractBridge<InputLayerDataType, InputLayerLayout, OutputLayerDataType,
      OutputLayerLayout, GPUDriver>(InputLayerType * const _p_input_layer,
          OutputLayerType * const _p_output_layer, GPUDriver * const _p_driver) :
        curr_B(_p_input_layer->p_data_cube->B),
        iR(_p_input_layer->p_data_cube->R), iC(_p_input_layer->p_data_cube->C),
        iD(_p_input_layer->p_data_cube->D), iB(_p_input_layer->p_data_cube->B),
        oR(_p_output_layer->p_data_cube->R), oC(_p_output_layer->p_data_cube->C),
        oD(_p_output_layer->p_data_cube->D), oB(_p_output_layer->p_data_cube->B),
        p_input_layer(_p_input_layer), p_output_layer(_p_output_layer),
        layer_param(NULL), solver_param(NULL), p_driver(_p_driver),
        bias_term(false) {

          // GPU: should not do softmax for now, can change later but then make sure we set iB to 1 below
          assert(false);
          // input_d_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(iR, iC, iD, iB, p_driver);
          // input_g_cube = new LogicalCube<InputLayerDataType, InputLayerLayout>(iR, iC, iD, iB, p_driver);
          // output_d_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(oR, oC, oD, oB, p_driver);
          // output_g_cube = new LogicalCube<OutputLayerDataType, OutputLayerLayout>(oR, oC, oD, oB, p_driver);
        }

    // This needs to be virtual, so we can delete the subclass bridges
    virtual ~AbstractBridge() {
      delete input_d_cube;  input_d_cube  = NULL;
      delete input_g_cube;  input_g_cube  = NULL;
      delete output_d_cube; output_d_cube = NULL;
      delete output_g_cube; output_g_cube = NULL;
    }
};
#endif // _INCLUDE_GPUDRIVER

#endif
