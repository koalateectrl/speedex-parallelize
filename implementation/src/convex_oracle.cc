#include "convex_oracle.h"
#include "simple_debug.h"

#include "gsl_utils.h"

namespace edce {


void
ConvexOracle::solve_f(std::vector<double>& prices) {
	auto& manager = management_structures.work_unit_manager;
	size_t problem_dimension = manager.get_total_nnz() * 2 + manager.get_num_assets();


	std::printf("starting solve: problem dim = %lu\n", problem_dimension);
	GslFuncF func(&function_oracle, problem_dimension, &ConvexFunctionOracle::eval_f_static);

	std::printf("make function\n");

	GslFMinimizer minimizer(gsl_multimin_fminimizer_nmsimplex2, problem_dimension, func);

	std::printf("make minimizer\n");

	function_oracle.set_feasible_starting_point(prices, minimizer.get_solution_ref());

	minimizer.reset_iteration();

	std::printf("Starting valuation is %lf\n", function_oracle.eval_f(minimizer.get_solution_ref().vec));

	std::printf("function ptr was %p\n", func.func.f);

	size_t iter_count = 0;
	int status;
	for (int i = 0; i < 20000; i++) {
		std::printf("starting round %d\n", i);

		do {
			status = minimizer.iterate_base();

			if (status) {
				std::printf("finish status=%lu, val=%lf\n", status, minimizer.get_latest_f());
				return;
			}

			if (iter_count % 1 == 0) {
				std::printf("current val: %lf\n", minimizer.get_latest_f());
			}
		} while(status == GSL_CONTINUE);

		std::printf("status=%ld\n", status);

		minimizer.copy_solution_to_vars();
		function_oracle.boost_barrier();
		minimizer.reset_iteration();
	}


	std::printf("function ptr was %p\n", func.func.f);




}


} /* edce */