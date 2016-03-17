/**
 *  Project: HipGISAXS
 *
 *  File: hipgisaxs_fit_pounders.cpp
 *  Created: Dec 26, 2013
 *
 *  Author: Slim Chourou <stchourou@lbl.gov>
 *          Abhinav Sarje <asarje@lbl.gov>
 *
 *  Licensing: The AnalyzeHipGISAXS software is only available to be downloaded and
 *  used by employees of academic research institutions, not-for-profit
 *  research laboratories, or governmental research facilities. Please read the
 *  accompanying LICENSE file before downloading the software. By downloading
 *  the software, you are agreeing to be bound by the terms of this
 *  NON-COMMERCIAL END USER LICENSE AGREEMENT.
 */

#include <iostream>

#include <analyzer/hipgisaxs_fit_pounders.hpp>

/*
f(X) - f(X*) (estimated)            <= fatol
|f(X) - f(X*)| (estimated) / |f(X)| <= frtol
||g(X)||                            <= gatol
||g(X)|| / |f(X)|                   <= grtol
||g(X)|| / ||g(X0)||                <= gttol
 */

namespace hig {

  bool FitPOUNDERSAlgo::run(int argc, char **argv, int algo_num, int img_num) {
    if(!(*obj_func_).set_reference_data(img_num)) return false;

    static char help[] = "** Attempting fitting using Pounders algorithm...";
    std::cout << help << " [ " << img_num << " ]" << std::endl;

    real_t pdelta, pnpmax, pgqt;
    int newnarg = argc;
    //char* newargs[argc + 3];     // possibly add arguments for the tao routines
    //char newargs[argc + 3][50];
    char** newargs = new char*[argc + 6];     // possibly add arguments for the tao routines
    for(int i = 0; i < argc; ++ i) {
      newargs[i] = new char[50];
      strncpy(newargs[i], argv[i], 50);
    } // for

    if(HiGInput::instance().analysis_algo_param(algo_num, "pounders_delta", pdelta)) {
      std::stringstream arg1; arg1 << "-tao_pounders_delta";
      //newargs[newnarg] = new char[arg1.str().size() + 1];
      newargs[newnarg] = new char[50];
      strncpy(newargs[newnarg], arg1.str().c_str(), 50);
      ++ newnarg;
      std::stringstream arg2; arg2 << pdelta;
      //newargs[newnarg] = new char[arg2.str().size() + 1];
      newargs[newnarg] = new char[50];
      strncpy(newargs[newnarg], arg2.str().c_str(), 50);
      ++ newnarg;
    } else {
      std::cerr << "warning: default pounders_delta being used" << std::endl;
    } // if-else
    if(HiGInput::instance().analysis_algo_param(algo_num, "pounders_npmax", pnpmax)) {
      std::stringstream arg1; arg1 << "-tao_pounders_npmax";
      //newargs[newnarg] = new char[arg1.str().size() + 1];
      newargs[newnarg] = new char[50];
      strncpy(newargs[newnarg], arg1.str().c_str(), 50);
      ++ newnarg;
      std::stringstream arg2; arg2 << pnpmax;
      //newargs[newnarg] = new char[arg2.str().size() + 1];
      newargs[newnarg] = new char[50];
      strncpy(newargs[newnarg], arg2.str().c_str(), 50);
      ++ newnarg;
    } else {
      std::cerr << "warning: default pounders_npmax being used" << std::endl;
    } // if-else
    if(HiGInput::instance().analysis_algo_param(algo_num, "pounders_gqt", pgqt)) {
      std::stringstream arg1; arg1 << "-tao_pounders_gqt";
      //newargs[newnarg] = new char[arg1.str().size() + 1];
      newargs[newnarg] = new char[50];
      strncpy(newargs[newnarg], arg1.str().c_str(), 50);
      ++ newnarg;
      std::stringstream arg2; arg2 << pgqt;
      //newargs[newnarg] = new char[arg2.str().size() + 1];
      newargs[newnarg] = new char[50];
      strncpy(newargs[newnarg], arg2.str().c_str(), 50);
      ++ newnarg;
    } else {
      std::cerr << "warning: default pounders_gqt being used" << std::endl;
    } // if-else

    // temp ...
    for(int i = 0; i < newnarg; ++ i) std::cout << newargs[i] << std::endl;

    int size, rank;
    PetscErrorCode ierr;
    PetscInitialize(&newnarg, &newargs, (char*) 0, help);
    TaoInitialize(&newnarg, &newargs, (char*) 0, help);

    // need to free the newargs memory ... TODO ...

    Vec x0;   // variables to read from context
    double y[1] = { 0 };
    VecCreateSeq(PETSC_COMM_SELF, num_params_, &x0);
    for(PetscInt i = 0; i < num_params_; ++ i) {
      y[0] = (double) x0_[i];
      VecSetValues(x0, 1, &i, y, INSERT_VALUES);
    } // for

    PetscReal tr_rad = 10;
    Vec f;
    PetscReal zero = 0.0;
    PetscReal hist[max_hist_], resid[max_hist_];
    PetscInt nhist;
    TaoSolver tao;
    TaoSolverTerminationReason reason;

    /* allocate vectors for the solution, gradient, hessian, etc. */
    ierr = VecCreateSeq(PETSC_COMM_SELF, num_obs_, &f);

    /* create TAO solver with pounders */
    ierr = TaoCreate(PETSC_COMM_SELF, &tao);
    //ierr = TaoSetType(tao, "tao_pounders");
    ierr = TaoSetType(tao, TAOPOUNDERS);
    /* set routines for function, gradient, hessian evaluation */
    ierr = TaoSetSeparableObjectiveRoutine(tao, f, EvaluateFunction, obj_func_);

    /* check for TAO command line options */
    ierr = TaoSetFromOptions(tao);
    TaoSetMaximumIterations(tao, max_iter_);
    #ifdef PETSC_36
      TaoSetHistory(tao, hist, resid, NULL, NULL, max_hist_, PETSC_TRUE);
    #else
      TaoSetHistory(tao, hist, resid, 0, max_hist_, PETSC_TRUE);
    #endif // PETSC_36
    TaoSetTolerances(tao, tol_, PETSC_DEFAULT, PETSC_DEFAULT, PETSC_DEFAULT, PETSC_DEFAULT);

    ierr = TaoSetInitialVector(tao, x0);
    ierr = TaoSolve(tao);
    ierr = TaoGetTerminationReason(tao, &reason);
    #ifdef PETSC_36
      TaoGetHistory(tao, NULL, NULL, NULL, NULL, &nhist);
    #else
      TaoGetHistory(tao, 0, 0, 0, &nhist);
    #endif

    PetscPrintf(PETSC_COMM_WORLD, "** History:\n");
    for(int i = 0; i < nhist; ++ i)
      PetscPrintf(PETSC_COMM_WORLD, "** %d:\t%g\t%g\n", i, hist[i], resid[i]);

    PetscInt iterate;
    PetscReal f_cv, gnorm, cnorm, xdiff, *x_cv;
    TaoGetSolutionStatus(tao, &iterate, &f_cv, &gnorm, &cnorm, &xdiff, &reason);
    TaoGetSolutionVector(tao, &x0);
    VecGetArray(x0, &x_cv);

    xn_.clear();
    for(PetscInt j = 0; j < num_params_; ++ j) {
      VecGetValues(x0, 1, &j, y);
      xn_.push_back(y[0]);
    } // for

    std::cout << "** Final vector: [ ";
    for(real_vec_t::iterator i = xn_.begin(); i != xn_.end(); ++ i) std::cout << *i << " ";
    std::cout << "]" << std::endl;

    ierr = TaoDestroy(&tao);
    ierr = VecDestroy(&x0);
    TaoFinalize();

    return true;
  } // FitPOUNDERSAlgo::run()


  void FitPOUNDERSAlgo::print() {
    // ...
  } // FitPOUNDERSAlgo::print()

} // namespace hig

