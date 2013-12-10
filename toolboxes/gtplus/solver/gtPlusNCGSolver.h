/** \file       gtPlusNCGSolver.h
    \brief      Implement the non-linear conjugate gradient solver for scalar function minimization problem
                The function to be optmized is required to supply the gradient computation

                The Secant line search is used with the non-linear CG solver.

    \author     Hui Xue

    Ref to:
    http://en.wikipedia.org/wiki/Nonlinear_conjugate_gradient_method
    http://en.wikipedia.org/wiki/Secant_method
*/

#pragma once

#include "gtPlusNonLinearSolver.h"

namespace Gadgetron { namespace gtPlus {

template <typename Array_Type_I, typename Array_Type_O, typename Oper_Type>
class gtPlusNCGSolver : public gtPlusNonLinearSolver<Array_Type_I, Array_Type_O, Oper_Type>
{
public:

    typedef gtPlusNonLinearSolver<Array_Type_I, Array_Type_O, Oper_Type> BaseClass;
    typedef typename BaseClass::ValueType ValueType;
    typedef typename realType<ValueType>::Type value_type;
    typedef typename BaseClass::Oper_Elem_Type Oper_Elem_Type;
    typedef typename BaseClass::Oper_List_Type Oper_List_Type;

    gtPlusNCGSolver();
    virtual ~gtPlusNCGSolver();

    virtual bool solve(const Array_Type_I& b, Array_Type_O& x);

    virtual bool grad(const Array_Type_I& x, Array_Type_I& grad);
    virtual bool obj(const Array_Type_I& x, ValueType& obj);

    virtual void printInfo(std::ostream& os) const;

    /// number of max iterations
    size_t iterMax_;

    /// threshold for detla change of gradient
    double gradThres_;

    /// threshold for detla change of objective function
    double objThres_;

    /// scale factor of initial step size of linear search 
    double beta_;

    /// initial step size of linear search
    double t0_;

    /// number of max linear search iterations (secant linear search)
    size_t secantIterMax_;

    /// gradient threshold for secant linear search
    double secantThres_;

    /// sometimes the secantThres can increase during line search
    /// the maximal allowed secantThres increments compared to previous secant iteration
    double secantRatio_;

    /// initial guess for the solver
    Array_Type_O* x0_;

    using BaseClass::gt_timer1_;
    using BaseClass::gt_timer2_;
    using BaseClass::gt_timer3_;
    using BaseClass::performTiming_;
    using BaseClass::printIter_;
    using BaseClass::gt_exporter_;
    using BaseClass::debugFolder_;
    using BaseClass::gtPlus_util_;
    using BaseClass::gtPlus_util_complex_;
    using BaseClass::gtPlus_mem_manager_;

protected:

    using BaseClass::callback_;
    using BaseClass::operList_;

    Array_Type_I gradBuf_;
};

template <typename Array_Type_I, typename Array_Type_O, typename Oper_Type>
gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::
gtPlusNCGSolver() : BaseClass()
{
    iterMax_ = 10;
    gradThres_ = 1e-4;
    objThres_ = 0.1;
    beta_ = 0.5;
    t0_ = 2.0;
    secantIterMax_ = 10;
    secantThres_ = 1e-3;
    secantRatio_ = 2;
}

template <typename Array_Type_I, typename Array_Type_O, typename Oper_Type>
gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::
~gtPlusNCGSolver() 
{

}

template <typename Array_Type_I, typename Array_Type_O, typename Oper_Type>
bool gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::
grad(const Array_Type_I& x, Array_Type_I& g)
{
    try
    {
        g.create(x.get_dimensions());

        size_t N = operList_.size();
        if ( N == 0 ) return true;

        GADGET_CHECK_RETURN_FALSE(operList_[0].first->grad(x, g));
        GADGET_CHECK_RETURN_FALSE(Gadgetron::scal(operList_[0].second, g));

        for ( size_t op=1; op<N; op++ )
        {
            GADGET_CHECK_RETURN_FALSE(operList_[op].first->grad(x, gradBuf_));
            GADGET_CHECK_RETURN_FALSE(Gadgetron::scal(operList_[op].second, gradBuf_));
            GADGET_CHECK_RETURN_FALSE(Gadgetron::add(gradBuf_, g, g));
        }
    }
    catch(...)
    {
        GADGET_ERROR_MSG("Errors happened in gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::grad(...) ... ");
        return false;
    }

    return true;
}

template <typename Array_Type_I, typename Array_Type_O, typename Oper_Type>
bool gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::
obj(const Array_Type_I& x, ValueType& ob)
{
    try
    {
        size_t N = operList_.size();
        if ( N == 0 )
        {
            ob = 0;
            return true;
        }

        GADGET_CHECK_RETURN_FALSE(operList_[0].first->obj(x, ob));
        ob *= operList_[0].second;

        ValueType v = 0;
        for ( size_t op=1; op<N; op++ )
        {
            GADGET_CHECK_RETURN_FALSE(operList_[op].first->obj(x, v));
            ob += operList_[op].second * v;
        }
    }
    catch(...)
    {
        GADGET_ERROR_MSG("Errors happened in gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::obj(...) ... ");
        return false;
    }

    return true;
}

template <typename Array_Type_I, typename Array_Type_O, typename Oper_Type>
bool gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::
solve(const Array_Type_I& /*b*/, Array_Type_O& x)
{
    try
    {
        if ( operList_.empty() ) return true;

        value_type v;

        // initial gradient
        Array_Type_I g0(*x0_);
        GADGET_CHECK_RETURN_FALSE(this->grad(*x0_, g0));

        //Gadgetron::norm2(*x0_, v); GADGET_MSG(v);
        //Gadgetron::norm2(g0, v); GADGET_MSG(v);

        // dx = -g0;
        Array_Type_I dx(g0);
        GADGET_CHECK_RETURN_FALSE( Gadgetron::scal( (value_type)(-1), dx ) );

        //Gadgetron::norm2(dx, v); GADGET_MSG(v);

        // initialize x
        x = *x0_;

        // secant parameters
        value_type bk, dxNorm, t0(t0_);
        ValueType oriF, prevF, currF, deltaD, thresValue, prevThresValue, phiPrev(0), v1, v2, v3, phi, alpha(0);
        Array_Type_I g1(g0), gTmp(g0), sx(*x0_), xTmp(*x0_), dxTmp(dx), prevX(*x0_);
        size_t nIter(0);

        // guess the t0_
        this->obj(x, oriF);

        dxTmp = dx;
        Gadgetron::scal(t0, dxTmp);
        Gadgetron::add(x, dxTmp, xTmp);

        this->obj(xTmp, currF);

        if (printIter_)
        {
            GADGET_MSG("To determine t0, --- ori and curr obj: " << oriF << " - " << currF << " ... ");
        }

        unsigned int numOfTries = 0;
        while ( (std::abs(currF.real() - oriF.real())/currF.real() < 0.05) && (numOfTries < 3) )
        {
            numOfTries++;

            t0 /= beta_;

            dxTmp = dx;
            Gadgetron::scal(t0, dxTmp);
            Gadgetron::add(x, dxTmp, xTmp);

            this->obj(xTmp, currF);

            GADGET_MSG("t0 is " << t0 << " ... ");
            GADGET_MSG("To determine t0, --- ori and curr obj: " << oriF << " - " << currF << " ... ");
        }

        prevF = oriF;
        while (1)
        {
            // secant line-search
            // wGradient(x+t0*dx);
            dxTmp = dx;
            Gadgetron::scal(t0, dxTmp);
            Gadgetron::add(x, dxTmp, xTmp);

            //Gadgetron::norm2(xTmp, v); GADGET_MSG(v);

            this->grad(xTmp, gTmp);

            //Gadgetron::norm2(gTmp, v); GADGET_MSG(v);

            // phiPrev = gTmp(:)'*dx(:);
            Gadgetron::dotc(gTmp, dx, phiPrev);
            alpha = -t0;
            Gadgetron::dotc(dx, dx, deltaD);

            thresValue = std::conj(alpha)*alpha*deltaD;
            prevThresValue = thresValue;

            size_t lsiter = 0;
            sx = x;

            while ( (lsiter<secantIterMax_) 
                && (thresValue.real()>secantThres_) 
                && (thresValue.real()<=secantRatio_*prevThresValue.real()) )
            {
                if ( lsiter == 0 )
                {
                    gTmp = g0;
                }
                else
                {
                    this->grad(sx, gTmp);
                }

                Gadgetron::dotc(gTmp, dx, phi);
                // alpha = alpha * (phi.real()/(phiPrev.real()-phi.real()));
                alpha = alpha * phi/(phiPrev-phi);
                phiPrev = phi;
                lsiter = lsiter+1;
                prevThresValue = std::abs(thresValue);
                thresValue = std::conj(alpha)*alpha*deltaD;

                if ( thresValue.real() <= secantRatio_*prevThresValue.real() )
                {
                    dxTmp = dx;
                    Gadgetron::scal(alpha, dxTmp);
                    Gadgetron::add(sx, dxTmp, sx);
                }
            }

            // control the number of line searches by adapting the initial step search
            if (lsiter>2)
            {
                t0 *= beta_;
            }

            if (lsiter<1)
            {
                t0 /= beta_;
            }

            prevX = x;
            x = sx;

            this->obj(x, currF);

            // conjugate gradient calculation
            this->grad(x, g1);

            // Fletcher - Reeves updates
            Gadgetron::dotc(g1, g1, v1);
            Gadgetron::dotc(g0, g0, v2);
            bk = v1.real()/(v2.real()+DBL_EPSILON);

            g0 = g1;

            // dx =  - g1 + bk.* dx;
            dxTmp = dx;
            Gadgetron::scal(bk, dxTmp);
            Gadgetron::subtract(dxTmp, g1, dx);

            if (printIter_)
            {
                GADGET_MSG("Iteration " << nIter << " --- prev and curr obj: " << prevF << " - " << currF << " - line search: " << lsiter);
            }

            // perform call back
            if ( callback_ != NULL )
            {
                Gadgetron::norm2(dx, dxNorm);
                if ( (nIter>iterMax_) || (dxNorm<gradThres_) || (callback_->exit() && (prevF.real()-currF.real()<objThres_)) )
                {
                    if ( prevF.real() < currF.real() )
                    {
                        x = prevX;
                    }
                    break;
                }

                GADGET_CHECK_RETURN_FALSE(callback_->callBack(nIter, x));
                GADGET_MSG("exit is " << callback_->exit());

                nIter = nIter + 1;
            }
            else
            {
                nIter = nIter + 1;

                Gadgetron::norm2(dx, dxNorm);
                if ( (nIter>iterMax_) || (dxNorm<gradThres_) || (prevF.real()-currF.real()<objThres_) )
                {
                    if ( prevF.real() < currF.real() )
                    {
                        x = prevX;
                    }
                    break;
                }
            }

            prevF = currF;
        }
    }
    catch(...)
    {
        GADGET_ERROR_MSG("Errors happened in gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::solve(...) ... ");
        return false;
    }

    return true;
}

template <typename Array_Type_I, typename Array_Type_O, typename Oper_Type>
void gtPlusNCGSolver<Array_Type_I, Array_Type_O, Oper_Type>::
printInfo(std::ostream& os) const
{
    using namespace std;
    os << "-------------- GTPlus ISMRMRD ncg solver -------------" << endl;
    os << "The non-linear cg solver " << std::endl;
    os << "------------------------------------------------------------" << endl;
}

}}
