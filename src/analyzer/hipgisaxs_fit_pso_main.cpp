/**
 *  Project:
 *
 *  File: pso.cpp
 *  Created: Jan 13, 2014
 *  Modified: Thu 20 Mar 2014 07:33:50 AM PDT
 *
 *  Author: Abhinav Sarje <asarje@lbl.gov>
 */

#include <analyzer/hipgisaxs_fit_pso.hpp>
#include <analyzer/objective_func_hipgisaxs.hpp>
#include <analyzer/hipgisaxs_ana.hpp>

int main(int narg, char** args) {
	if(narg != 7) {
		std::cout << "usage: hipgisaxs_pso <input_config> <num_particles> <num_generations> "
			<< "<omega> <phi1> <phi2>"
			<< std::endl;
		return 1;
	} // if

	//AbsoluteDifferenceError err;
	//AbsoluteDifferenceNorm err;
	AbsoluteDifferenceSquareNorm err;
	hig::HipGISAXSObjectiveFunction hip_func(narg, args, &err);
	hig::ParticleSwarmOptimization my_pso(narg, args, &hip_func,
											atoi(args[4]), atoi(args[5]), atoi(args[6]),
											atoi(args[2]), atoi(args[3]));
	hig::HipGISAXSAnalyzer ana;
	ana.add_analysis_algo(&my_pso);

	woo::BoostChronoTimer maintimer;

	maintimer.start();
	ana.analyze(narg, args, 1);
	maintimer.stop();

	hig::parameter_map_t result = my_pso.get_best_values();
	if(my_pso.is_master()) {
		std::cout << "** ** Final parameter values: " << std::endl;
		for(hig::parameter_map_t::const_iterator i = result.begin(); i != result.end(); ++ i)
			std::cout << "      ++ " << (*i).first << " = " << (*i).second << std::endl;
		std::cout << "** ** TOTAL ANALYSIS TIME: " << maintimer.elapsed_msec() << " ms. ** **"
					<< std::endl;
	} // if

	return 0;
} // main()
