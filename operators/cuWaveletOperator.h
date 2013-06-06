#pragma once

#include "linearOperator.h"
#include "cuNDArray_operators.h"
#include "cuNDArray.h"

namespace Gadgetron {

template<class T, unsigned int D> class cuWaveletOperator : public linearOperator<cuNDArray<T> >{

public:
	cuWaveletOperator() : linearOperator<cuNDArray<T> >(){};

	virtual ~cuWaveletOperator(){};
	virtual void mult_M(cuNDArray<T>*,cuNDArray<T>*,bool );
	virtual void mult_MH(cuNDArray<T>*,cuNDArray<T>*,bool );
	virtual void mult_MH_M(cuNDArray<T>* in ,cuNDArray<T>* out,bool accumulate ){
		if (accumulate){
			*out += *in;
		} else {
			*out = *in;
		}
	}
	virtual boost::shared_ptr< linearOperator< cuNDArray<T>  > >  clone(){
				return linearOperator< cuNDArray<T> >::clone(this);
			}
	virtual void set_domain_dimensions(std::vector<unsigned int>* dims);

};

}
