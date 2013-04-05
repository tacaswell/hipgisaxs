/***
  *  Project:
  *
  *  File: parameters_mic.hpp
  *  Created: Apr 02, 2013
  *  Modified: Fri 05 Apr 2013 10:21:22 AM PDT
  *
  *  Author: Abhinav Sarje <asarje@lbl.gov>
  */

#ifndef __PARAMETERS_MIC_HPP__
#define __PARAMETERS_MIC_HPP__

namespace hig {

	/***
	 * tunable parameters for computations on MIC
	 */

	// hyperblock size
	const int MIC_BLOCK_X_ = 100;
	const int MIC_BLOCK_Y_ = 40;
	const int MIC_BLOCK_Z_ = 30;
	const int MIC_BLOCK_T_ = 2500;

	// number of OMP threads on MIC
	__attribute__((target(mic)))
	const int MIC_OMP_NUM_THREADS_ = 240;

} // namespace hig

#endif // __PARAMETERS_MIC_HPP__