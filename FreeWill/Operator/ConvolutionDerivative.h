#ifndef CONVOLUTIONDERIVATIVE_H
#define CONVOLUTIONDERIVATIVE_H

#include "Operator.h"
#include "../Context/Context.h"

namespace FreeWill
{

    template<DeviceType DeviceUsed = DeviceType::CPU_NAIVE, typename DataType = float>
    class ConvolutionDerivative : public Operator<DeviceUsed>
    {
    protected:
        using Operator<DeviceUsed>::input;
        using Operator<DeviceUsed>::output;
        using Operator<DeviceUsed>::m_deviceId;

        unsigned int m_strideX;
        unsigned int m_strideY;
        unsigned int m_zeroPaddingX;
        unsigned int m_zeroPaddingY;

        cudnnTensorDescriptor_t m_prevActivationGPUTensorDescriptor;
        cudnnTensorDescriptor_t m_outputDeltaGPUTensorDescriptor;
        cudnnTensorDescriptor_t m_biasGradGPUTensorDescriptor;
        cudnnTensorDescriptor_t m_prevActivationDeltaGPUTensorDescriptor;
        cudnnFilterDescriptor_t m_featureMapFilterDescriptor;
        cudnnConvolutionDescriptor_t m_convolutionDescriptor;
        cudnnConvolutionBwdFilterAlgo_t m_filterBackwardAlgorithm;
        cudnnConvolutionBwdDataAlgo_t m_prevActivationDeltaAlgorithm;
        unsigned char *m_filterBackwardAlgorithmWorkspace;
        size_t m_filterBackwardAlgorithmWorkspaceSize;
        unsigned char *m_prevActivationDeltaAlgorithmWorkspace;
        size_t m_prevActivationDeltaAlgorithmWorkspaceSize;


    public:
        ConvolutionDerivative(unsigned int strideX = 1, unsigned int strideY = 1,
                unsigned int zeroPaddingX = 0, unsigned int zeroPaddingY = 0, unsigned int deviceId = 0)
            :Operator<DeviceUsed>({"PrevActivation","OutputGrad","FeatureMap"},{"FeatureMapGrad","BiasGrad","InputGrad"}, deviceId),
            m_strideX(strideX),
            m_strideY(strideY),
            m_zeroPaddingX(zeroPaddingX),
            m_zeroPaddingY(zeroPaddingY),
            m_prevActivationGPUTensorDescriptor(0),
            m_outputDeltaGPUTensorDescriptor(0),
            m_biasGradGPUTensorDescriptor(0),
            m_prevActivationDeltaGPUTensorDescriptor(0),
            m_featureMapFilterDescriptor(0),
            m_convolutionDescriptor(0),
            m_filterBackwardAlgorithm(),
            m_prevActivationDeltaAlgorithm(),
            m_filterBackwardAlgorithmWorkspace(nullptr),
            m_filterBackwardAlgorithmWorkspaceSize(0),
            m_prevActivationDeltaAlgorithmWorkspace(nullptr),
            m_prevActivationDeltaAlgorithmWorkspaceSize(0)
        {
            CHECK_GPU;
            if (DeviceUsed == DeviceType::GPU_CUDA)
            {
                RUN_CUDNN(cudnnCreateTensorDescriptor(&m_prevActivationGPUTensorDescriptor));
                RUN_CUDNN(cudnnCreateTensorDescriptor(&m_outputDeltaGPUTensorDescriptor));
                RUN_CUDNN(cudnnCreateTensorDescriptor(&m_biasGradGPUTensorDescriptor));
                RUN_CUDNN(cudnnCreateTensorDescriptor(&m_prevActivationDeltaGPUTensorDescriptor));
                RUN_CUDNN(cudnnCreateFilterDescriptor(&m_featureMapFilterDescriptor));
                RUN_CUDNN(cudnnCreateConvolutionDescriptor(&m_convolutionDescriptor));
            }
        }

        ~ConvolutionDerivative()
        {
            CHECK_GPU;
            if (DeviceUsed == DeviceType::GPU_CUDA)
            {
                RUN_CUDNN(cudnnDestroyTensorDescriptor(m_prevActivationGPUTensorDescriptor));
                RUN_CUDNN(cudnnDestroyTensorDescriptor(m_outputDeltaGPUTensorDescriptor));
                RUN_CUDNN(cudnnDestroyTensorDescriptor(m_biasGradGPUTensorDescriptor));
                RUN_CUDNN(cudnnDestroyTensorDescriptor(m_prevActivationDeltaGPUTensorDescriptor));
                RUN_CUDNN(cudnnDestroyFilterDescriptor(m_featureMapFilterDescriptor));
                RUN_CUDNN(cudnnDestroyConvolutionDescriptor(m_convolutionDescriptor));

                m_prevActivationGPUTensorDescriptor = 0;
                m_outputDeltaGPUTensorDescriptor = 0;
                m_biasGradGPUTensorDescriptor = 0;
                m_prevActivationDeltaGPUTensorDescriptor = 0;
                m_featureMapFilterDescriptor = 0;
                m_convolutionDescriptor = 0;

                if (m_filterBackwardAlgorithmWorkspace)
                {
                    RUN_CUDA(cudaFree(m_filterBackwardAlgorithmWorkspace));
                    m_filterBackwardAlgorithmWorkspace = nullptr;
                }

                if (m_prevActivationDeltaAlgorithmWorkspace)
                {
                    RUN_CUDA(cudaFree(m_prevActivationDeltaAlgorithmWorkspace));
                    m_prevActivationDeltaAlgorithmWorkspace = nullptr;
                }
            }
        }

        void displayFilterBackwardAlgorithm(cudnnConvolutionBwdFilterAlgo_t algorithm)
        {
            QString message = "Convolution filter bacward algorithm:";
            switch (algorithm)
            {
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_FILTER_ALGO_0, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_FILTER_ALGO_1, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_FILTER_ALGO_FFT, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_FILTER_ALGO_3, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_FILTER_ALGO_WINOGRAD_NONFUSED, message)
            default:
                qDebug() << "Warning: unrecognized convolution filter backward algorithm:" << algorithm;
                break;
            }
        }

        void displayPrevActivationDeltaAlgorithm(cudnnConvolutionBwdDataAlgo_t algorithm)
        {
            QString message = "Convolution PrevActivation delta algorithm:";
            switch (algorithm)
            {
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_DATA_ALGO_0, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_DATA_ALGO_1, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_DATA_ALGO_FFT, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_DATA_ALGO_FFT_TILING, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_DATA_ALGO_WINOGRAD, message)
            ENUM_CASE(CUDNN_CONVOLUTION_BWD_DATA_ALGO_WINOGRAD_NONFUSED, message)
            default:
                qDebug() << "Warning: unrecognized convolution filter backward algorithm:" << algorithm;
                break;
            }           
        }

        virtual bool init() override
        {
            CHECK_GPU;
            FAIL_IF (!input("PrevActivation") || !input("OutputGrad") || !input("FeatureMap"));

            FAIL_IF (!output("FeatureMapGrad") || !output("BiasGrad") || !output("InputGrad"));

            FAIL_IF (input("PrevActivation")->shape()[0] != input("FeatureMap")->shape()[0]);

            FAIL_IF (input("PrevActivation")->shape().dimension() != 4);

            FAIL_IF (input("FeatureMap")->shape().dimension() != 4);

            FAIL_IF (input("OutputGrad")->shape().dimension() != 4);

            FAIL_IF (input("FeatureMap")->shape()[1] != input("FeatureMap")->shape()[2]);

            FAIL_IF (output("InputGrad")->shape() != input("PrevActivation")->shape());

            unsigned int originalWidth = input("PrevActivation")->shape()[1];
            unsigned int originalHeight = input("PrevActivation")->shape()[2];
            unsigned int filterSize = input("FeatureMap")->shape()[1];

            FAIL_IF ((originalWidth - filterSize + 2*m_zeroPaddingX) % m_strideX != 0);

            FAIL_IF ((originalHeight - filterSize + 2*m_zeroPaddingY) % m_strideY !=0);

            unsigned int newWidth = (originalWidth - filterSize + 2*m_zeroPaddingX) / m_strideX + 1;
            unsigned int newHeight = (originalHeight - filterSize + 2*m_zeroPaddingY) / m_strideY + 1;

            //qDebug() << "output" << output("Output")->shape()[1] <<";"<< output("Output")->shape()[2];

            FAIL_IF (input("OutputGrad")->shape()[1] != newWidth || input("OutputGrad")->shape()[2] != newHeight);

            FAIL_IF (output("BiasGrad")->shape().dimension() != 1);

            FAIL_IF (output("BiasGrad")->shape()[0] != input("FeatureMap")->shape()[3]);

            FAIL_IF (input("FeatureMap")->shape()[3] != input("OutputGrad")->shape()[0]);

            FAIL_IF (input("PrevActivation")->shape()[3] != input("OutputGrad")->shape()[3]);

            FAIL_IF (input("FeatureMap")->shape() != output("FeatureMapGrad")->shape());

            if constexpr (DeviceUsed == DeviceType::GPU_CUDA)
            {
                unsigned int channelCount = input("PrevActivation")->shape()[0];
                unsigned int batchSize = input("PrevActivation")->shape()[3];

                unsigned int filterCount = input("FeatureMap")->shape()[3];

                cudnnDataType_t dataType = CUDNN_DATA_FLOAT;
                if constexpr (std::is_same<DataType,float>::value)
                {
                    dataType = CUDNN_DATA_FLOAT;
                }
                else if constexpr (std::is_same<DataType,double>::value)
                {
                    dataType = CUDNN_DATA_DOUBLE;
                }

                RUN_CUDNN(cudnnSetTensor4dDescriptor( m_prevActivationGPUTensorDescriptor,
                                                      CUDNN_TENSOR_NHWC,
                                                      dataType,
                                                      batchSize,
                                                      channelCount,
                                                      originalHeight,
                                                      originalWidth));

                RUN_CUDNN(cudnnSetTensor4dDescriptor( m_outputDeltaGPUTensorDescriptor,
                                                      CUDNN_TENSOR_NHWC,
                                                      dataType,
                                                      batchSize,
                                                      filterCount,
                                                      newHeight,
                                                      newWidth));


                RUN_CUDNN(cudnnSetTensor4dDescriptor( m_biasGradGPUTensorDescriptor,
                                                      CUDNN_TENSOR_NHWC,
                                                      dataType,
                                                      1,
                                                      filterCount,
                                                      1,
                                                      1));

                RUN_CUDNN(cudnnSetTensor4dDescriptor( m_prevActivationGPUTensorDescriptor,
                                                      CUDNN_TENSOR_NHWC,
                                                      dataType,
                                                      batchSize,
                                                      channelCount,
                                                      originalHeight,
                                                      originalWidth));

                RUN_CUDNN(cudnnSetTensor4dDescriptor( m_prevActivationDeltaGPUTensorDescriptor,
                                                      CUDNN_TENSOR_NHWC,
                                                      dataType,
                                                      batchSize,
                                                      channelCount,
                                                      originalHeight,
                                                      originalWidth));




                //qDebug() << "filterCount" << filterCount << "channelCount" << channelCount;

                RUN_CUDNN(cudnnSetFilter4dDescriptor( m_featureMapFilterDescriptor,
                                                      dataType,
                                                      CUDNN_TENSOR_NHWC,
                                                      filterCount,
                                                      channelCount,
                                                      filterSize,
                                                      filterSize));

                cudnnDataType_t cudnnDataType = cudnnDataType_t::CUDNN_DATA_FLOAT;

                if constexpr (std::is_same<DataType, float>::value)
                {
                   cudnnDataType = cudnnDataType_t::CUDNN_DATA_FLOAT;
                }
                else if constexpr (std::is_same<DataType, double>::value)
                {
                   cudnnDataType = cudnnDataType_t::CUDNN_DATA_DOUBLE;
                }

                //qDebug() <<"zero padding stride:" << m_zeroPaddingX << m_zeroPaddingY << m_strideX << m_strideY;
                RUN_CUDNN(cudnnSetConvolution2dDescriptor( m_convolutionDescriptor,
                                                           m_zeroPaddingY ,
                                                           m_zeroPaddingX ,
                                                           m_strideY,
                                                           m_strideX,
                                                           1,
                                                           1,
                                                           CUDNN_CROSS_CORRELATION, cudnnDataType));


                RUN_CUDNN(cudnnGetConvolutionBackwardFilterAlgorithm( Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                                      m_prevActivationGPUTensorDescriptor,
                                                                      m_outputDeltaGPUTensorDescriptor,
                                                                      m_convolutionDescriptor,
                                                                      m_featureMapFilterDescriptor,
                                                                      CUDNN_CONVOLUTION_BWD_FILTER_PREFER_FASTEST,
                                                                      0,
                                                                      &m_filterBackwardAlgorithm ));

                displayFilterBackwardAlgorithm(m_filterBackwardAlgorithm);

                RUN_CUDNN(cudnnGetConvolutionBackwardFilterWorkspaceSize(Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                                         m_prevActivationGPUTensorDescriptor,
                                                                         m_outputDeltaGPUTensorDescriptor,
                                                                         m_convolutionDescriptor,
                                                                         m_featureMapFilterDescriptor,
                                                                         m_filterBackwardAlgorithm,
                                                                         &m_filterBackwardAlgorithmWorkspaceSize));

                qDebug() << "workspace size:" << m_filterBackwardAlgorithmWorkspaceSize;

                const int requestedFilterAlgoCount = 6;
                cudnnConvolutionBwdFilterAlgoPerf_t filterBackwardPerfResults[requestedFilterAlgoCount];
                int returnedFilterAlgoCount = 0;

                RUN_CUDNN(cudnnFindConvolutionBackwardFilterAlgorithm( Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                                       m_prevActivationGPUTensorDescriptor,
                                                                       m_outputDeltaGPUTensorDescriptor,
                                                                       m_convolutionDescriptor,
                                                                       m_featureMapFilterDescriptor,
                                                                       requestedFilterAlgoCount,
                                                                       &returnedFilterAlgoCount,
                                                                       filterBackwardPerfResults ));

                qDebug() << returnedFilterAlgoCount << "convolution filter backward algorithm benchmarks:";

                for(int i =0;i<returnedFilterAlgoCount;++i)
                {
                    qDebug() << i << "Status:" << filterBackwardPerfResults[i].status 
                        << "Time:" << filterBackwardPerfResults[i].time << "milliseconds" 
                        << "Memory need:" << filterBackwardPerfResults[i].memory;

                    displayFilterBackwardAlgorithm(filterBackwardPerfResults[i].algo);
                }

                if (filterBackwardPerfResults[0].algo != m_filterBackwardAlgorithm)
                {
                    m_filterBackwardAlgorithm = filterBackwardPerfResults[0].algo;
                    m_filterBackwardAlgorithmWorkspaceSize = filterBackwardPerfResults[0].memory;
                }

                if (m_filterBackwardAlgorithmWorkspaceSize > 0)
                {
                    RUN_CUDA(cudaMalloc(&m_filterBackwardAlgorithmWorkspace, m_filterBackwardAlgorithmWorkspaceSize));
                }

                qDebug() << "----------------------------------------------------------------------------";


                RUN_CUDNN(cudnnGetConvolutionBackwardDataAlgorithm( Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                                    m_featureMapFilterDescriptor,
                                                                    m_outputDeltaGPUTensorDescriptor,
                                                                    m_convolutionDescriptor,
                                                                    m_prevActivationDeltaGPUTensorDescriptor,
                                                                    CUDNN_CONVOLUTION_BWD_DATA_PREFER_FASTEST,
                                                                    0,
                                                                    &m_prevActivationDeltaAlgorithm));

                displayPrevActivationDeltaAlgorithm(m_prevActivationDeltaAlgorithm);

                RUN_CUDNN(cudnnGetConvolutionBackwardDataWorkspaceSize( Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                                        m_featureMapFilterDescriptor,
                                                                        m_outputDeltaGPUTensorDescriptor,
                                                                        m_convolutionDescriptor,
                                                                        m_prevActivationDeltaGPUTensorDescriptor,
                                                                        m_prevActivationDeltaAlgorithm,
                                                                        &m_prevActivationDeltaAlgorithmWorkspaceSize));

                qDebug() << "workspace size:" << m_prevActivationDeltaAlgorithmWorkspaceSize;

                const int requestedPrevActivationAlgoCount = 6;
                cudnnConvolutionBwdDataAlgoPerf_t prevActivationPerfResults[requestedPrevActivationAlgoCount];
                int returnedPrevActivationAlgoCount = 0;

                RUN_CUDNN(cudnnFindConvolutionBackwardDataAlgorithm( Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                                     m_featureMapFilterDescriptor,
                                                                     m_outputDeltaGPUTensorDescriptor,
                                                                     m_convolutionDescriptor,
                                                                     m_prevActivationDeltaGPUTensorDescriptor,
                                                                     requestedPrevActivationAlgoCount,
                                                                     &returnedPrevActivationAlgoCount,
                                                                     prevActivationPerfResults));

                for(int i =0;i<returnedPrevActivationAlgoCount;++i)
                {
                    qDebug() << i << "Status:" << prevActivationPerfResults[i].status
                        << "Time:" << prevActivationPerfResults[i].time << "milliseconds"
                        << "Memory need:" << prevActivationPerfResults[i].memory;

                    displayPrevActivationDeltaAlgorithm(prevActivationPerfResults[i].algo);
                }

                if (prevActivationPerfResults[0].algo != m_prevActivationDeltaAlgorithm)
                {
                    m_prevActivationDeltaAlgorithm = prevActivationPerfResults[0].algo;
                    m_prevActivationDeltaAlgorithmWorkspaceSize = prevActivationPerfResults[0].memory;
                }

                if (m_prevActivationDeltaAlgorithmWorkspaceSize > 0)
                {
                    RUN_CUDA(cudaMalloc(&m_prevActivationDeltaAlgorithmWorkspace, m_prevActivationDeltaAlgorithmWorkspaceSize));
                }

                qDebug() << "----------------------------------------------------------------------------";
                
            }

            return true;
        }

        virtual void evaluate() override
        {
            CHECK_GPU;
            Tensor<DeviceUsed, DataType> *_prevActivation = input("PrevActivation")->template toType<DataType>();
            Tensor<DeviceUsed, DataType> *_featureMap = input("FeatureMap")->template toType<DataType>();
            Tensor<DeviceUsed, DataType> *_outputGrad = input("OutputGrad")->template toType<DataType>();

            Tensor<DeviceUsed, DataType> *_featureMapGrad = output("FeatureMapGrad")->template toType<DataType>();
            Tensor<DeviceUsed, DataType> *_biasGrad = output("BiasGrad")->template toType<DataType>();
            Tensor<DeviceUsed, DataType> *_inputGrad = output("InputGrad")->template toType<DataType>();

            unsigned int featureMapCount = _featureMap->shape()[3];
            unsigned int featureMapLength = _featureMap->shape()[1];

            unsigned int originalWidth = _prevActivation->shape()[1];
            unsigned int originalHeight = _prevActivation->shape()[2];

            unsigned int channelCount = _featureMap->shape()[0];

            
            unsigned int newWidth = (originalWidth - featureMapLength + 2 * m_zeroPaddingX ) / m_strideX + 1;
            unsigned int newHeight = (originalHeight - featureMapLength + 2 * m_zeroPaddingY) / m_strideY + 1;

            unsigned int batchSize = _prevActivation->shape()[3];
            if constexpr (DeviceUsed == DeviceType::CPU_NAIVE)
            {
                for (unsigned int b = 0; b < batchSize; ++b)
                {
                    for(unsigned int newIndexY = 0; newIndexY < newHeight;++newIndexY)
                    {
                        for (unsigned int newIndexX = 0; newIndexX < newWidth;++newIndexX)
                        {

                            int startX = -m_zeroPaddingX + newIndexX * m_strideX;
                            int startY = -m_zeroPaddingY + newIndexY * m_strideY;

                            unsigned int resultBaseIndex = (b * newWidth*newHeight +newIndexY * newWidth + newIndexX) * featureMapCount; 

                            for (unsigned int k = 0; k < featureMapCount; ++k)
                            {

                                for(int y = 0; y< (int)featureMapLength; ++y)
                                {
                                    for(int x = 0; x < (int)featureMapLength; ++x)
                                    {
                                        int realX = x + startX;
                                        int realY = y + startY;

                                        if ((realX >= 0 && realX < (int)originalWidth) 
                                                && (realY>=0 && realY< (int)originalHeight))
                                        {
                                            unsigned int originalBaseIndex = (b* originalHeight * originalWidth + realY*originalWidth + realX)
                                                *channelCount;
                                            unsigned int featureMapBaseIndex = (k*(featureMapLength * featureMapLength) + y*featureMapLength + x) * channelCount; 
                                
                                            for(unsigned int c = 0;c<channelCount;++c)
                                            {
                                                /*(*_output)[resultBaseIndex + k] += 
                                                    (*_featureMap)[(k * (featureMapLength * featureMapLength) + 
                                                            y*featureMapLength +x) * channelCount + c]
                                                    * (*_input)[originalBaseIndex + c];
                                                */

                                                (*_featureMapGrad)[featureMapBaseIndex + c]
                                                    += (*_outputGrad)[resultBaseIndex + k] * (*_prevActivation)[originalBaseIndex + c];

                                                (*_inputGrad)[originalBaseIndex + c] += (*_featureMap)[(k * (featureMapLength * featureMapLength) + 
                                                    y*featureMapLength + x)*channelCount + c] * (*_outputGrad)[resultBaseIndex + k];
                                             
                                            }
                                        
                                            //qDebug() << "base index" << realX << ";" << realY << ";" <<originalWidth <<";"<< originalBaseIndex;
                                            //qDebug() << "feature map" << (*_featureMap)[(k * (featureMapLength * featureMapLength) +
                                            //            y*featureMapLength +x) * channelCount + 0];                                   
                                            //qDebug() << (*_input)[originalBaseIndex ];
                                        }
                                    }
                                }

                                //qDebug() << "result loc" << resultBaseIndex + k;
                                (*_biasGrad)[k] += (*_outputGrad)[resultBaseIndex + k];
                                //(*_output)[resultBaseIndex + k] += (*_bias)[k]; 
                            }
                        }
                    }                
                }

                //DataType scale = 1.0 / (newWidth * newHeight);


               /* for(unsigned int k = 0;k<_biasGrad->shape().size();++k)
                {
                    (*_biasGrad)[k] *= scale;
                }*/

/*                for(unsigned int i = 0;i< _featureMapGrad->shape().size();++i)
                {
                    (*_featureMapGrad)[i] *= scale;
                }
*/
/*                for(unsigned int i =0;i<_inputGrad->shape().size(); ++i)
                {
                    (*_inputGrad)[i] *= scale;
                }
*/
            }
            else if constexpr (DeviceUsed == DeviceType::GPU_CUDA )
            {
                DataType alpha = 1.0;
                DataType beta = 0.0;

                RUN_CUDNN(cudnnConvolutionBackwardFilter(Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                        &alpha,
                                                        m_prevActivationGPUTensorDescriptor,
                                                        _prevActivation->gpuDataHandle(),
                                                        m_outputDeltaGPUTensorDescriptor,
                                                        _outputGrad->gpuDataHandle(),
                                                        m_convolutionDescriptor,
                                                        m_filterBackwardAlgorithm,
                                                        m_filterBackwardAlgorithmWorkspace,
                                                        m_filterBackwardAlgorithmWorkspaceSize,
                                                        &beta,
                                                        m_featureMapFilterDescriptor,
                                                        _featureMapGrad->gpuDataHandle()));

                RUN_CUDNN(cudnnConvolutionBackwardBias(Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                       &alpha,
                                                       m_outputDeltaGPUTensorDescriptor,
                                                       _outputGrad->gpuDataHandle(),
                                                       &beta,
                                                       m_biasGradGPUTensorDescriptor,
                                                       _biasGrad->gpuDataHandle()));

                RUN_CUDNN(cudnnConvolutionBackwardData(Context<DeviceUsed>::getSingleton().cudnnHandle(m_deviceId),
                                                       &alpha,
                                                       m_featureMapFilterDescriptor,
                                                       _featureMap->gpuDataHandle(),
                                                       m_outputDeltaGPUTensorDescriptor,
                                                       _outputGrad->gpuDataHandle(),
                                                       m_convolutionDescriptor,
                                                       m_prevActivationDeltaAlgorithm,
                                                       m_prevActivationDeltaAlgorithmWorkspace,
                                                       m_prevActivationDeltaAlgorithmWorkspaceSize,
                                                       &beta,
                                                       m_prevActivationDeltaGPUTensorDescriptor,
                                                       _inputGrad->gpuDataHandle()));
            }
            

        }

    };
}

#endif
