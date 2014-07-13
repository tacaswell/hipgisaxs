/**
 *  Project: HipGISAXS (High-Performance GISAXS Data Analysis)
 *
 *  File: ObjFct.hpp
 *  Created: Dec 26, 2013
 *  Modified: Sun 02 Feb 2014 10:13:10 AM PST
 *  Description: Main class that computes the objective fct given the ref data, (forward) simulation model (HipGISAXS inp object)
 *  and a handle to error/distance computing class (e.g. L2-norm)
 *
 *  Author: Slim Chourou <stchourou@lbl.gov>
 *  Developers: Slim Chourou <stchourou@lbl.gov>
 *              Abhinav Sarje <asarje@lbl.gov>
 *              Alexander Hexemer <ahexemer@lbl.gov>
 *              Xiaoye Li <xsli@lbl.gov>
 *
 *  Licensing: The AnalyzeHipGISAXS software is only available to be downloaded and
 *  used by employees of academic research institutions, not-for-profit
 *  research laboratories, or governmental research facilities. Please read the
 *  accompanying LICENSE file before downloading the software. By downloading
 *  the software, you are agreeing to be bound by the terms of this
 *  NON-COMMERCIAL END USER LICENSE AGREEMENT.
 */

#ifndef _OBJFCT_HPP_
#define _OBJFCT_HPP_

#include <iostream>

//#include <analyzer/SimModel.hpp>
#include <analyzer/ImageData.hpp>
#include <analyzer/typedefs.hpp>
#include <analyzer/distance_functions.hpp>
#include <hipgisaxs.hpp>

#include <tao.h>

namespace hig{


  class ObjFct{
  private :
    //Dist* pdist_;
	DistanceMeasure* pdist_;
    ImageData* pdata_ref_;
    //SimModel* psim_;  /* stores the computed sim. data */
    HipGISAXS* psim_;  /* stores the computed sim. data */

    float_vec_t f_x_; /* the error/dist computed between ref and sim data  */
    //float_vec_t J_x_; /* the Jacobian matrix  */
    float deriv_stp_;
    bool is_valid_;

    /*Nbr of obs. points parallel & vertical*/
    int n_par_;
    int n_ver_;
    int num_params_; /* dimension of param space  */

  public:
    ObjFct() { deriv_stp_= 0.1; }
    ObjFct(DistanceMeasure* pdist, ImageData* pref, HipGISAXS* psim, int dim) {
      pdist_ = pdist ;
      pdata_ref_ = pref;
      n_par_ = pdata_ref_->n_par();
      n_ver_ = pdata_ref_->n_ver();
      num_params_ = dim;
      psim_ = psim;
      is_valid_=true;
      deriv_stp_= 0.1;
      std::cout << "Creating Obj Fct with " <<    n_par_ << "x" << n_ver_  <<  " observations\n" ;
      //      psim_->init();
    }
    //ObjFct(DistanceMeasure* pdist, ImageData* pref, SimModel* psim, int dim){

    ~ObjFct(){}
    bool init(){is_valid_=false; return is_valid_;}

    /*  setters */
//    void set_sim(SimModel* psim)  { psim_ = psim; is_valid_=true; psim_->init(); }
    void set_dist(DistanceMeasure* pdist)  { pdist_ = pdist; is_valid_=true; }
    //void set_dist(DistanceMeasure* pdist)  { pdist_ = pdist; is_valid_=true; }
    void set_ref_data(ImageData* pdata_ref) { pdata_ref_ = pdata_ref; is_valid_=true; }
    void set_dim(int dim) { num_params_ = dim;  }

    /* getters  */
    float_mat_t get_f_x() {if(is_valid_) return f_x_; /* else return {0} ; */ }
    int get_dim(){ return num_params_;}
    int get_nobs(){ return n_par_ * n_ver_;}

    /* computer   */
    float_mat_t compute(); /* uses Dist member definition to compute error/distance value */
    float_mat_t compute(float_vec_t X);
    float_mat_t compute_test(float_vec_t X);
    //float_mat_t compute_jacobian(float_vec_t X, int dim);
    PetscReal* tao_compute(PetscReal* x);
    Mat tao_compute_jacobian(Vec X);

	float_vec_t operator()(float_vec_t);

  }; /* class ObjFct */

  PetscErrorCode EvaluateFunction(TaoSolver , Vec , Vec , void *);
  PetscErrorCode EvaluateJacobian(TaoSolver , Vec , Mat *, Mat *, MatStructure*,void *);

} /* namespace hig */

#endif /* OBJFCT_HPP_ */