//
//  Layer.h
//  moka
//
//  Created by Ce Zhang on 1/13/15.
//  Copyright (c) 2015 Hazy Research. All rights reserved.
//

#include "LogicalCube.h"
#include "Connector.h"
#include "Kernel.h"
#include "Report.h"

#ifndef moka_Layer_h
#define moka_Layer_h

/**
 * One Layer contains three things:
 *   - [DATA] A LogicalCube that is the Data in this Layer;
 *   - [MODEL] A LogicalCube that is the Model which we will use for the Forward Pass;
 *   - [GRADIENT] A LogicalCube that is the Gradient of Data which we will use for Backward Pass.
 **/
template
<typename DataType, LayoutType DataLayout>
class Layer{
public:
    
    static Layer<DataType, DataLayout> * make_layer(const int N, const int I, const int B, const int K, const int O){
        return new Layer<DataType, DataLayout>(
           new LogicalCube<DataType, DataLayout>(N, N, I, B),
           new LogicalCube<DataType, DataLayout>(K, K, I, O),
           new LogicalCube<DataType, DataLayout>(N, N, I, B)
        );
    }
    
    typedef LogicalCube<DataType, DataLayout> DataLogicalCubeType;
    typedef LogicalCube<DataType, DataLayout> ModelLogicalCubeType;
    typedef LogicalCube<DataType, DataLayout> GradientLogicalCubeType;
    
    const size_t dR, dC, dD, dB;
    const size_t mR, mC, mD, mB;
    const size_t gR, gC, gD, gB;
    
    DataLogicalCubeType * const p_data_cube;
    ModelLogicalCubeType * const p_model_cube;
    GradientLogicalCubeType * const p_gradient_cube;
    
    Layer(DataLogicalCubeType * const _p_data_cube,
          ModelLogicalCubeType * const _p_model_cube,
          GradientLogicalCubeType * const _p_gradient_cube) :
        p_data_cube(_p_data_cube),
        p_model_cube(_p_model_cube),
        p_gradient_cube(_p_gradient_cube),
        dR(_p_data_cube->R), dC(_p_data_cube->C), dD(_p_data_cube->D), dB(_p_data_cube->B),
        mR(_p_model_cube->R), mC(_p_model_cube->C), mD(_p_model_cube->D), mB(_p_model_cube->B),
        gR(_p_gradient_cube->R), gC(_p_gradient_cube->C), gD(_p_gradient_cube->D), gB(_p_gradient_cube->B)
    {
#ifdef _DO_ASSERT
        assert(dR==gR); assert(dC==gC); assert(dD==gD); assert(dB==gB);
#endif
    }
};

#endif




