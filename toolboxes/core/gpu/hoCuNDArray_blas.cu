#include "hoCuNDArray_blas.h"
#include "cuNDArray_blas.h"
#include "complext.h"
#include "check_CUDA.h"

namespace Gadgetron{

#define CUBLAS_CALL(fun) {cublasStatus_t err = fun; if (err != CUBLAS_STATUS_SUCCESS) {throw cuda_error(gadgetron_getCublasErrorString(err));}}

  // These are defined in cuNDArray_blas.cu
  //

  template<class T> EXPORTGPUCORE cublasStatus_t cublas_axpy(cublasHandle_t hndl, int n, const T* a , const T* x , int incx,  T* y, int incy);
  template<class T> EXPORTGPUCORE cublasStatus_t cublas_dot(cublasHandle_t, int, const T*, int, const  T*, int, T*, bool cc = true);
  template<class T> EXPORTGPUCORE cublasStatus_t cublas_nrm2(cublasHandle_t, int, const T*, int, typename realType<T>::Type *result);
  template<class T> EXPORTGPUCORE cublasStatus_t cublas_amax(cublasHandle_t handle, int n,const T *x, int incx, int *result);
  template<class T> EXPORTGPUCORE cublasStatus_t cublas_amin(cublasHandle_t handle, int n,const T *x, int incx, int *result);
  template<class T> EXPORTGPUCORE cublasStatus_t cublas_asum(cublasHandle_t handle, int n,const T *x, int incx, typename realType<T>::Type *result);

  template<class T> EXPORTGPUCORE void axpy( T a, hoCuNDArray<T>* x, hoCuNDArray<T>* y )
  {
    int device = cudaDeviceManager::Instance()->getCurrentDevice();
    size_t free = cudaDeviceManager::Instance()->getFreeMemory(device);
    size_t batchSize = 1024*1024*(free/(sizeof(T)*2*1024*1024)); //Ensure 1Mb allocations
    size_t remaining = x->get_number_of_elements();
    batchSize = std::min(batchSize,remaining);
    T* x_ptr = x->get_data_ptr();
    T* y_ptr = y->get_data_ptr();
    std::vector<size_t> dims;
    dims.push_back(batchSize);
    cuNDArray<T> cuX(&dims);
    cuNDArray<T> cuY(&dims);

    for (size_t i = 0; i < (x->get_number_of_elements()-1)/batchSize+1; i++){

      size_t curSize = std::min(batchSize,remaining);

      CUDA_CALL(cudaMemcpy(cuX.get_data_ptr(),x_ptr+i*batchSize,curSize*sizeof(T),cudaMemcpyHostToDevice));
      CUDA_CALL(cudaMemcpy(cuY.get_data_ptr(),y_ptr+i*batchSize,curSize*sizeof(T),cudaMemcpyHostToDevice));

      CUBLAS_CALL(cublas_axpy(cudaDeviceManager::Instance()->lockHandle(device), curSize,
			      &a, cuX.get_data_ptr(), 1, cuY.get_data_ptr(), 1));

      cudaDeviceManager::Instance()->unlockHandle(device);
    
      CUDA_CALL(cudaMemcpy(y_ptr,cuY.get_data_ptr(),curSize*sizeof(T),cudaMemcpyDeviceToHost));
      remaining -= batchSize;
    }
  }

  template<class T> EXPORTGPUCORE void axpy( T a, hoCuNDArray< complext<T> >*x, hoCuNDArray< complext<T> > *y )
  {
    axpy( complext<T>(a), x, y );
  }

  template<class T> EXPORTGPUCORE T dot( hoCuNDArray<T> *x, hoCuNDArray<T> *y, bool cc )
  {
    int device = cudaDeviceManager::Instance()->getCurrentDevice();
    size_t free = cudaDeviceManager::Instance()->getFreeMemory(device);
    size_t batchSize = 1024*1024*(free/(sizeof(T)*2*1024*1024)); //Ensure 1Mb allocations
    size_t remaining = x->get_number_of_elements();
    batchSize = std::min(batchSize,remaining);
    T* x_ptr = x->get_data_ptr();
    T* y_ptr = y->get_data_ptr();
    std::vector<size_t> dims;
    dims.push_back(batchSize);
    cuNDArray<T> cuX(&dims);
    cuNDArray<T> cuY(&dims);
    T ret = T(0);

    for (size_t i = 0; i < (x->get_number_of_elements()-1)/batchSize+1; i++){
    
      size_t curSize = std::min(batchSize,remaining);

      CUDA_CALL(cudaMemcpy(cuX.get_data_ptr(),x_ptr+i*batchSize,curSize*sizeof(T),cudaMemcpyHostToDevice));
      CUDA_CALL(cudaMemcpy(cuY.get_data_ptr(),y_ptr+i*batchSize,curSize*sizeof(T),cudaMemcpyHostToDevice));

      T cur_ret;
      CUBLAS_CALL(cublas_dot( cudaDeviceManager::Instance()->lockHandle(device), curSize,
			      cuX.get_data_ptr(), 1,
			      cuY.get_data_ptr(), 1,
			      &cur_ret, cc ));

      cudaDeviceManager::Instance()->unlockHandle(device);

      remaining -= batchSize;
      ret += cur_ret;
    }
    return ret;
  }

  template<class T> EXPORTGPUCORE typename realType<T>::Type nrm2( hoCuNDArray<T>* x )
  {
    typedef typename realType<T>::Type REAL;
    int device = cudaDeviceManager::Instance()->getCurrentDevice();
    size_t free = cudaDeviceManager::Instance()->getFreeMemory(device);
    size_t batchSize = 1024*1024*(free/(sizeof(T)*1024*1024)); //Ensure 1Mb allocations
    size_t remaining = x->get_number_of_elements();
    batchSize = std::min(batchSize,remaining);
    T* x_ptr = x->get_data_ptr();
    std::vector<size_t> dims;
    dims.push_back(batchSize);
    cuNDArray<T> cuX(&dims);
    REAL ret = 0;

    for (size_t i = 0; i < (x->get_number_of_elements()-1)/batchSize+1; i++){

      size_t curSize = std::min(batchSize,remaining);
      CUDA_CALL(cudaMemcpy(cuX.get_data_ptr(),x_ptr+i*batchSize,curSize*sizeof(T),cudaMemcpyHostToDevice));

      REAL cur_ret;
      CUBLAS_CALL(cublas_nrm2<T>( cudaDeviceManager::Instance()->lockHandle(device), batchSize,
				  cuX.get_data_ptr(), 1, &cur_ret));

      cudaDeviceManager::Instance()->unlockHandle(device);

      remaining -= batchSize;
      ret += cur_ret*cur_ret;
    }
    return std::sqrt(ret);
  }

  template<class T> EXPORTGPUCORE typename realType<T>::Type asum( hoCuNDArray<T>* x )
  {
    typedef typename realType<T>::Type REAL;
    int device = cudaDeviceManager::Instance()->getCurrentDevice();
    size_t free = cudaDeviceManager::Instance()->getFreeMemory(device);
    size_t batchSize = 1024*1024*(free/(sizeof(T)*1024*1024)); //Ensure 1Mb allocations
    size_t remaining = x->get_number_of_elements();
    batchSize = std::min(batchSize,remaining);
    T* x_ptr = x->get_data_ptr();
    std::vector<size_t> dims;
    dims.push_back(batchSize);
    cuNDArray<T> cuX(&dims);
    REAL ret = 0;

    for (size_t i = 0; i < (x->get_number_of_elements()-1)/batchSize+1; i++){

      size_t curSize = std::min(batchSize,remaining);
      CUDA_CALL(cudaMemcpy(cuX.get_data_ptr(),x_ptr+i*batchSize,curSize*sizeof(T),cudaMemcpyHostToDevice));

      REAL cur_ret;
      CUBLAS_CALL(cublas_asum( cudaDeviceManager::Instance()->lockHandle(device), batchSize,
			       cuX.get_data_ptr(), 1,
			       &cur_ret));

      cudaDeviceManager::Instance()->unlockHandle(device);

      remaining -= batchSize;
      ret += cur_ret;
    }
    return ret;
  }

  template<class T> EXPORTGPUCORE int amin( hoCuNDArray<T>* x )
  {
    int device = cudaDeviceManager::Instance()->getCurrentDevice();
    size_t free = cudaDeviceManager::Instance()->getFreeMemory(device);
    size_t batchSize = 1024*1024*(free/(sizeof(T)*1024*1024)); //Ensure 1Mb allocations
    size_t remaining = x->get_number_of_elements();
    batchSize = std::min(batchSize,remaining);
    T* x_ptr = x->get_data_ptr();
    std::vector<size_t> dims;
    dims.push_back(batchSize);
    cuNDArray<T> cuX(&dims);
    std::vector<int> results;
 
    for (size_t i = 0; i < (x->get_number_of_elements()-1)/batchSize+1; i++){

      size_t curSize = std::min(batchSize,remaining);
      CUDA_CALL(cudaMemcpy(cuX.get_data_ptr(),x_ptr+i*batchSize,curSize*sizeof(T),cudaMemcpyHostToDevice));

      int cur_ret;
      CUBLAS_CALL(cublas_amin( cudaDeviceManager::Instance()->lockHandle(device), batchSize,
			       cuX.get_data_ptr(), 1,
			       &cur_ret));

      cudaDeviceManager::Instance()->unlockHandle(device);

      remaining -= batchSize;
      results.push_back(cur_ret+i*batchSize-1);
    }

    int res =0;
    for (int i =0; i < results.size(); i++){
      if (abs(x_ptr[results[i]]) < abs(x_ptr[res])) res = results[i];
    }
    return res;
  }

  template<class T> EXPORTGPUCORE int amax( hoCuNDArray<T>* x )
  {
    int device = cudaDeviceManager::Instance()->getCurrentDevice();
    size_t free = cudaDeviceManager::Instance()->getFreeMemory(device);
    size_t batchSize = 1024*1024*(free/(sizeof(T)*1024*1024)); //Ensure 1Mb allocations
    size_t remaining = x->get_number_of_elements();
    batchSize = std::min(batchSize,remaining);
    T* x_ptr = x->get_data_ptr();
    std::vector<size_t> dims;
    dims.push_back(batchSize);
    cuNDArray<T> cuX(&dims);
    std::vector<int> results;

    for (size_t i = 0; i < (x->get_number_of_elements()-1)/batchSize+1; i++){

      size_t curSize = std::min(batchSize,remaining);
      CUDA_CALL(cudaMemcpy(cuX.get_data_ptr(),x_ptr+i*batchSize,curSize*sizeof(T),cudaMemcpyHostToDevice));

      int cur_ret;
      CUBLAS_CALL(cublas_amax( cudaDeviceManager::Instance()->lockHandle(device), batchSize,
			       cuX.get_data_ptr(), 1,
			       &cur_ret));

      cudaDeviceManager::Instance()->unlockHandle(device);

      remaining -= batchSize;
      results.push_back(cur_ret+i*batchSize-1);
    }

    int res =0;
    for (int i =0; i < results.size(); i++){
      if (abs(x_ptr[results[i]]) > abs(x_ptr[res])) res = results[i];
    }
    return res;
  }

  //
  // Instantiation
  //

  template EXPORTGPUCORE float dot(hoCuNDArray<float>*,hoCuNDArray<float>*,bool);
  template EXPORTGPUCORE float nrm2(hoCuNDArray<float>*);
  template EXPORTGPUCORE void axpy(float,hoCuNDArray<float>*,hoCuNDArray<float>*);
  template EXPORTGPUCORE int amin(hoCuNDArray<float>*);
  template EXPORTGPUCORE int amax(hoCuNDArray<float>*);
  template EXPORTGPUCORE float asum(hoCuNDArray<float>*);

  template EXPORTGPUCORE double dot(hoCuNDArray<double>*,hoCuNDArray<double>*,bool);
  template EXPORTGPUCORE double nrm2(hoCuNDArray<double>*);
  template EXPORTGPUCORE void axpy(double,hoCuNDArray<double>*,hoCuNDArray<double>*);
  template EXPORTGPUCORE int amin(hoCuNDArray<double>*);
  template EXPORTGPUCORE int amax(hoCuNDArray<double>*);
  template EXPORTGPUCORE double asum(hoCuNDArray<double>*);

  template EXPORTGPUCORE float_complext dot(hoCuNDArray<float_complext>*,hoCuNDArray<float_complext>*,bool);
  template EXPORTGPUCORE float nrm2(hoCuNDArray<float_complext>*);
  template EXPORTGPUCORE void axpy(float_complext,hoCuNDArray<float_complext>*,hoCuNDArray<float_complext>*);
  template EXPORTGPUCORE void axpy(float,hoCuNDArray<float_complext>*,hoCuNDArray<float_complext>*);
  template EXPORTGPUCORE int amin(hoCuNDArray<float_complext>*);
  template EXPORTGPUCORE int amax(hoCuNDArray<float_complext>*);
  template EXPORTGPUCORE float asum(hoCuNDArray<float_complext>*);

  template EXPORTGPUCORE double_complext dot(hoCuNDArray<double_complext>*,hoCuNDArray<double_complext>*,bool);
  template EXPORTGPUCORE double nrm2(hoCuNDArray<double_complext>*);
  template EXPORTGPUCORE void axpy(double_complext,hoCuNDArray<double_complext>*,hoCuNDArray<double_complext>*);
  template EXPORTGPUCORE void axpy(double,hoCuNDArray<double_complext>*,hoCuNDArray<double_complext>*);
  template EXPORTGPUCORE int amin(hoCuNDArray<double_complext>*);
  template EXPORTGPUCORE int amax(hoCuNDArray<double_complext>*);
  template EXPORTGPUCORE double asum(hoCuNDArray<double_complext>*);
}
