#include <gsl/gsl_vector.h>
#include <gsl/gsl_multimin.h>
#include <gsl/gsl_deriv.h>

#include <cstdio>
#include <random>
#include <chrono>
#include <cmath>

struct Order {
	int sell_asset;
	int buy_asset;
	int endow;
	double ratio;
};

struct Params {
	Order* orders;
	int num_assets;
	int num_agents;
	double barrier_param;
};

Order* get_orders(int num_agents, int num_assets) {
	Order* out = new Order[num_agents];

	std::mt19937 gen(0);

	std::uniform_int_distribution<> endow_dist (1, 1);
	std::uniform_int_distribution<> asset_dist (0, num_assets-1);
	std::uniform_real_distribution<> ratio_dist(1.4, 1.5);

	for (int i = 0; i < num_agents; i++) {
		out[i].sell_asset = asset_dist(gen);
		out[i].buy_asset = asset_dist(gen);
		while (out[i].buy_asset == out[i].sell_asset) {
			out[i].buy_asset = asset_dist(gen);
		}
		out[i].endow = 1;//endow_dist(gen);
		out[i].ratio = 2.0;//ratio_dist(gen);
	}
	return out;
}

#define TAX_CONSTANT_FEE 1.0
#define TAX_RATE 0.5

double f_nobarriers(const gsl_vector* v, void* void_params) {
	Params* params = (Params*) void_params;
	Order* orders = params->orders;

	double barrier = params->barrier_param;

	double f_out = 0.0;

	double *y_demands = new double[params->num_assets];
	double *y_supplies = new double[params->num_assets];
	for (int i = 0; i < params->num_assets; i++) {
		y_supplies[i] = 0;
		y_demands[i] = 0;
	}
	for (int i = 0; i < params->num_agents; i++) {

		//function
		int endow = orders[i].endow;
		double ratio = orders[i].ratio;


		double y = gsl_vector_get(v, params->num_assets + 2 * i);
		double beta = gsl_vector_get(v, params->num_assets + 2 * i + 1);

		double sell_price = gsl_vector_get(v, orders[i].sell_asset);
		double buy_price = gsl_vector_get(v, orders[i].buy_asset);

		//objective
		f_out += endow * sell_price * std::log(sell_price / beta) - y * std::log(ratio);
		//std::printf("%f\n", f_out);


	}


	delete[] y_demands;
	delete[] y_supplies;

	return f_out;

}

double f(const gsl_vector* v, void* void_params) {
	Params* params = (Params*) void_params;
	Order* orders = params->orders;

	double barrier = params->barrier_param;

	double f_out = 0.0;

	double *y_demands = new double[params->num_assets];
	double *y_supplies = new double[params->num_assets];

	for (int i = 0; i < params->num_assets; i++) {
		y_supplies[i] = 0;
		y_demands[i] = 0;
	}



	for (int i = 0; i < params->num_agents; i++) {

		//function
		int endow = orders[i].endow;
		double ratio = orders[i].ratio;


		double y = gsl_vector_get(v, params->num_assets + 2 * i);
		double beta = gsl_vector_get(v, params->num_assets + 2 * i + 1);

		double sell_price = gsl_vector_get(v, orders[i].sell_asset);
		double buy_price = gsl_vector_get(v, orders[i].buy_asset);

		//objective
		f_out += endow * sell_price * std::log(sell_price / beta) - y * std::log(ratio);
		//std::printf("%f\n", f_out);

		y_demands[orders[i].sell_asset] += y;
		y_supplies[orders[i].buy_asset] += y;

		//barriers

		//not overspending
		f_out += -(1/barrier)* std::log(sell_price * endow - y);

		//beta valid
		f_out += -(1/barrier) * std::log(sell_price - beta);
		f_out += -(1/barrier) * std::log(buy_price - ratio*beta);

		//positivity
		f_out += -(1/barrier) * std::log(beta);
		f_out += -(1/barrier) * std::log(y + 0.01);
	}

	for (int i = 0; i < params->num_assets; i++) {
		f_out += -(1/barrier) * std::log(y_supplies[i] - y_demands[i] * TAX_RATE + TAX_CONSTANT_FEE);
		f_out += -(1/barrier) * std::log(gsl_vector_get(v, i) - 1);
	}
	delete[] y_demands;
	delete[] y_supplies;


	if (std::isnan(f_out)) {
		return DBL_MAX;
	}
	return f_out;
}

void df(const gsl_vector *v, void *void_params, gsl_vector *df) {
	Params* params = (Params*) void_params;
	Order* orders = params->orders;

	double barrier = params->barrier_param;

	double *y_demands = new double[params->num_assets];
	double *y_supplies = new double[params->num_assets];

	for (int i = 0; i < params->num_assets; i++) {
		gsl_vector_set(df, i, 0);
		y_demands[i] = 0;
		y_supplies[i] = 0;
	}

	for (int i = 0; i < params->num_agents; i++) {

		//function
		int endow = orders[i].endow;
		double ratio = orders[i].ratio;

		double y = gsl_vector_get(v, params->num_assets + 2 * i);
		double beta = gsl_vector_get(v, params->num_assets + 2 * i + 1);

		double sell_price = gsl_vector_get(v, orders[i].sell_asset);
		double buy_price = gsl_vector_get(v, orders[i].buy_asset);

		double sell_price_deriv = endow * (std::log(sell_price/beta) + 1.0);

		double y_deriv = -std::log(ratio);

		//y > 0 barrier
		y_deriv += -(1/barrier) * (1/(y+0.01));

		//not overspending barrier for y pov
		y_deriv += -(1/barrier) * (1/(sell_price * endow - y)) * (-1);

		double beta_deriv = -sell_price * endow / beta;

		//beta > 0 barrier
		beta_deriv += -(1/barrier) * (1/beta);

		//beta valid barriers
		beta_deriv += -(1/barrier) * (1/(sell_price - beta)) * (-1);
		beta_deriv += -(1/barrier) * (1/(buy_price - ratio*beta)) * (-ratio);

		double sell_price_deriv_update = 0.0;
		double buy_price_deriv_update = 0.0;

		sell_price_deriv_update += sell_price_deriv;

		//for beta valid barriers
		sell_price_deriv_update += -(1/barrier) * (1/(sell_price - beta));
		buy_price_deriv_update += -(1/barrier) * (1/(buy_price - ratio * beta));

		//for not overspending barrier from price pov
		sell_price_deriv_update += -(1/barrier) * (1/(sell_price * endow - y)) * endow;

		gsl_vector_set(df, params->num_assets + 2 * i, y_deriv);
		gsl_vector_set(df, params->num_assets + 2 * i + 1, beta_deriv);

		gsl_vector_set(df, orders[i].sell_asset, gsl_vector_get(df, orders[i].sell_asset) + sell_price_deriv_update);
		gsl_vector_set(df, orders[i].buy_asset, gsl_vector_get(df, orders[i].buy_asset) + buy_price_deriv_update);

		y_demands[orders[i].sell_asset] += y;
		y_supplies[orders[i].buy_asset] += y;

	}

	for (int i = 0; i < params->num_agents; i++) {
		double sell_asset_diff = y_supplies[orders[i].sell_asset] - y_demands[orders[i].sell_asset] * TAX_RATE + TAX_CONSTANT_FEE;
		double buy_asset_diff = y_supplies[orders[i].buy_asset] - y_demands[orders[i].buy_asset] * TAX_RATE + TAX_CONSTANT_FEE;

		double y_sell_deriv_update = -(1/barrier) * (1/(sell_asset_diff)) * 1;
		double y_buy_deriv_update = -(1/barrier) * (1/(buy_asset_diff)) * -TAX_RATE;

		gsl_vector_set(df, 2 * i + params->num_assets, gsl_vector_get(df, 2 * i + params->num_assets) + y_sell_deriv_update + y_buy_deriv_update);
	}


	// for supply constraint barrier

	for (int i = 0; i < params->num_assets; i++) {
		double adjust = -(1/barrier) * (1/(gsl_vector_get(v, i) - 1)) * 1;

		gsl_vector_set(df, i, gsl_vector_get(df, i) + adjust);
	}

	delete[] y_demands;
	delete[] y_supplies;
}

void fdf(const gsl_vector *v, void *params, double *f_out, gsl_vector *df_out) {
	*f_out = f(v, params);
	df(v, params, df_out);
}

gsl_vector* get_valid_start(Order* orders, int num_assets, int num_agents) {
	gsl_vector *start = gsl_vector_alloc(num_assets + 2 * num_agents);
	double starting_price = 200;
	for (int i = 0; i < num_assets; i++) {
		gsl_vector_set(start, i, 200.0);
	}
	for (int i = 0; i < num_agents; i++) {
		gsl_vector_set(start, 2*i + num_assets, 0.01);
		gsl_vector_set(start, 2 * i + 1 + num_assets, std::min(starting_price, starting_price/orders[i].ratio) / 1.1);
	}
	return start;
}


struct DerivParams {
	void* params;
	int index;
	gsl_vector* point;
};

double point_eval(double val, void* void_params) {
	DerivParams* deriv_params = (DerivParams*) void_params;
	Params* params = (Params*) deriv_params->params;
	int num_variables = params->num_assets + 2 * params->num_agents;
	gsl_vector * loc = gsl_vector_alloc(num_variables);
	gsl_vector_memcpy(loc, deriv_params -> point);
	gsl_vector_set(loc, deriv_params->index, gsl_vector_get(loc, deriv_params->index) + val);
	return f(loc, params);
}

void numerical_derivative_check(int num_agents, int num_assets) {
	gsl_function F;
	double result, abserr;

	int num_variables = num_assets + num_agents * 2;

	Order* orders = get_orders(num_agents, num_assets);

	Params param;
	param.orders = orders;
	param.barrier_param = 100.0;
	param.num_assets = num_assets;
	param.num_agents = num_agents;

	DerivParams deriv_params;
	deriv_params.params = (void*) (&param);

	gsl_vector* starting_point = get_valid_start(orders, num_assets, num_agents);

	deriv_params.point = starting_point;

	gsl_vector* computed_deriv = gsl_vector_alloc(num_variables);
	df(starting_point, (void*)&param, computed_deriv);

	F.function = point_eval;
	F.params = (void*)(&deriv_params);

	for (int i = 0; i < num_variables; i++) {
		deriv_params.index = i;
		double deriv = gsl_deriv_central(&F, 0, 1e-8, &result, &abserr);

		std::printf("%d\tempirical %f computed %f\t\t%f\n", i, result, gsl_vector_get(computed_deriv, i), result - gsl_vector_get(computed_deriv, i));
	}
}

gsl_vector* get_step_size(int num_variables) {
	gsl_vector *step_size = gsl_vector_alloc(num_variables);
	for (int i = 0; i < num_variables; i++) {
		gsl_vector_set(step_size, i, 1);
	}
	return step_size;
}

bool good_status(int status) {
	return status == GSL_SUCCESS || status == GSL_CONTINUE;
}

void run_experiment(int num_agents, int num_assets, double tax_rate, double smooth_mult) {
	Order* orders = get_orders(num_agents, num_assets);

	Params param;
	param.orders = orders;
	param.barrier_param = 10;
	param.num_assets = num_assets;
	param.num_agents = num_agents;


	int num_variables = num_assets + num_agents * 2;
	//encoding: price (y, beta)_agent


	gsl_multimin_function_fdf my_func;
	my_func.n = num_variables;
	my_func.f = f;
	my_func.df = df;
	my_func.fdf = fdf;
	my_func.params = (void*) (&param);

	gsl_multimin_function my_func_noderiv;
	my_func_noderiv.n = num_variables;
	my_func_noderiv.f = f;
	my_func_noderiv.params = (void*) (&param);

	gsl_vector* loc = get_valid_start(orders, num_assets, num_agents);

	const gsl_multimin_fminimizer_type *T_noderiv;
	gsl_multimin_fminimizer *s_noderiv;
	T_noderiv = gsl_multimin_fminimizer_nmsimplex2;
	s_noderiv = gsl_multimin_fminimizer_alloc(T_noderiv, num_variables);
	gsl_vector* step_size = get_step_size(num_variables);

	const gsl_multimin_fdfminimizer_type *T;
	gsl_multimin_fdfminimizer *s;
	T = gsl_multimin_fdfminimizer_vector_bfgs2;
	s = gsl_multimin_fdfminimizer_alloc(T, num_variables);

	gsl_multimin_fminimizer_set(s_noderiv, &my_func_noderiv, loc, step_size);
	gsl_multimin_fdfminimizer_set(s, &my_func, loc, 0.1, 0.1);
#if 0
	std::size_t iter;
	double cur_val = 10;
	int status;

	do {
		iter++;
		status = gsl_multimin_fminimizer_iterate(s_noderiv);
		if (status) break;

		cur_val = gsl_multimin_fminimizer_minimum(s_noderiv);
		if (iter % 1000 == 0) {
			std::printf("iter %d %f\n", iter, cur_val);
		}
	} while (cur_val > 1 && iter < 90000);

#endif

#if 1
	std::size_t iter = 0;
	int status;

	gsl_vector* computed_deriv = gsl_vector_alloc(num_variables);

	do {
		iter++;
		status = gsl_multimin_fdfminimizer_iterate(s);

		if (!good_status(status)) {
			std::printf("iter status %d\n", status);
		}
		//df(s->x, (void*)&param, computed_deriv);
		else {
			status = gsl_multimin_test_gradient(s->gradient, 1e-3);
			if (!good_status(status)) {
				std::printf("gradient status %d\n", status);
				break;
			}
		}


		if (iter % 1000 == 0 || !good_status(status)) {
			std::printf("iter %d gradient:\n", iter);
			for (int i = 0; i < num_variables; i++) {
				std::printf("%f ", gsl_vector_get(s->gradient, i));
			}
			std::printf("\nvalue\n");
			for (int i = 0; i < num_variables; i++) {
				std::printf("%f ", gsl_vector_get(s->x, i));
			}
			std::printf("\n%f\t%f\n", f(s->x, (void*)&param), f_nobarriers(s->x, (void*)&param));
		}
	} while (iter < 100000 && good_status(status));
#endif


}

int main() {
	run_experiment(20, 3, 0.01, 0.01);
	//numerical_derivative_check(10, 20);
}