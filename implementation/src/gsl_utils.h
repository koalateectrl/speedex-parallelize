#pragma once

#include <gsl/gsl_vector.h>
#include <gsl/gsl_multimin.h>

namespace edce {

struct UniqueGslVec {
	gsl_vector * vec = nullptr;

	UniqueGslVec(size_t n)
		: vec(gsl_vector_calloc(n)) {}

	UniqueGslVec(const UniqueGslVec&) = delete;

	UniqueGslVec(UniqueGslVec&& other)
		: vec(other.vec) {
			other.vec = nullptr;
		}

	UniqueGslVec(gsl_vector* vec) : vec(vec) {}

	gsl_vector* release() {
		gsl_vector* out = vec;
		vec = nullptr;
		return out;
	}

	~UniqueGslVec() {
		if (vec != nullptr) {
			gsl_vector_free(vec);
		}
	}
};

/*
template<typename ParamsType>
static double 
eval_func_f_(const gsl_vector* vars, void* params) {
	ParamsType* params_interpreted = static_cast<ParamsType*>(params);

	return params_interpreted -> eval_f(vars);
}

template<typename ParamsType>
static void
eval_func_fdf_(const gsl_vector* vars, void* params, double* f, gsl_vector* df) {
	ParamsType* params_interpreted = static_cast<ParamsType*>(params);
	params_interpreted -> eval_fdf(vars, f, df);
}*/

struct GslFuncF {

	using func_ptr_t = double(*)(const gsl_vector*, void*);

	gsl_multimin_function func;

	GslFuncF(void* params, size_t problem_dimension, func_ptr_t func_ptr)//double (*func_ptr)(const gsl_vector*, void*))
		: func() {
			func.n = problem_dimension;
			func.f = func_ptr;/*[] (const gsl_vector* vars, void* params) -> double {
				std::printf("starting evaluation fn\n");
				ParamsType* params_interpreted = static_cast<ParamsType*>(params);
				return params_interpreted -> eval_f(vars);
			};*/
			func.params = params;//static_cast<void*>(params);

			std::printf("f=%p params = %p n=%lu, func=%p\n", func.f, func.params, func.n, &func);
		}

};

class GslFMinimizer {

	gsl_multimin_fminimizer *minimizer;
	gsl_vector* step_sizes;
	UniqueGslVec vars;
	gsl_multimin_function function;

public:

	GslFMinimizer(
		const gsl_multimin_fminimizer_type* type, 
		size_t problem_dimension,
		GslFuncF func)
		: minimizer(gsl_multimin_fminimizer_alloc(type, problem_dimension))
		, step_sizes(gsl_vector_alloc(problem_dimension))
		, vars(problem_dimension)
		, function(func.func) {
			
			gsl_vector_set_all(step_sizes, 1);

			/*std::printf("starting ctor, problem dim is %lu\n", problem_dimension);

			std::printf("making minimizer %lu %lu\n", vars.vec->size, step_sizes->size);

			std::printf("function.f = %p\n", function.f);
			std::printf("this=%p &function=%p\n", this, &function);

			gsl_multimin_fminimizer_set(minimizer, &function, vars.vec, step_sizes);

			std::printf("done making minimizer\n");*/
		}

	GslFMinimizer(const GslFMinimizer&) = delete;

	~GslFMinimizer() {
		gsl_multimin_fminimizer_free(minimizer);
	}

	int iterate_base() {
		return gsl_multimin_fminimizer_iterate(minimizer);
	}

	void reset_iteration(double step_size_default = 1.0) {
		//gsl_vector_set_all(step_sizes, step_size_default);
		gsl_multimin_fminimizer_set(minimizer, &function, vars.vec, step_sizes);
	}

	double get_latest_f() {
		return minimizer->fval;
	}

	void copy_solution_to_vars() {
		gsl_vector_memcpy(vars.vec, minimizer -> x);
	}

	UniqueGslVec extract_solution() {
		return UniqueGslVec(vars.release());
	}

	UniqueGslVec& get_solution_ref() {
		return vars;
	}
};


} /* edce */