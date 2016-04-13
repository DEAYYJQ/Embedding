#pragma once
#include "Import.hpp"
#include "ModelConfig.hpp"
#include "DataModel.hpp"
#include "Model.hpp"

class LatentModel
	:public Model
{
protected:
	field<vec>		embedding_head;
	field<vec>		embedding_tail;
	vec				embedding_topic;
	const int		n_topic;

protected:
	field<vec>		embedding_head_comp;
	field<vec>		embedding_tail_comp;
	vec				embedding_topic_comp;
	vector<vec>		embedding_relation;

public:
	LatentModel(		
		const Dataset& dataset,
		const TaskType& task_type,
		const string& logging_base_path,
		int topic)
		:Model(dataset, task_type, logging_base_path), n_topic(topic)
	{
			embedding_head = field<vec>(count_relation(), count_entity());
			for(auto i=embedding_head.begin(); i!=embedding_head.end(); ++i)
			{
				*i = randu(n_topic,1) * sqrt(6.0/n_topic);
			}

			embedding_tail = field<vec>(count_relation(), count_entity());
			for(auto i=embedding_tail.begin(); i!=embedding_tail.end(); ++i)
			{
				*i = randu(n_topic,1) * sqrt(6.0/n_topic);
			}

			embedding_topic.resize(n_topic);
			embedding_topic.fill(1.0/n_topic);
			normalize_to_prob();

			embedding_head_comp = field<vec>(count_relation(), count_entity());
			embedding_tail_comp = field<vec>(count_relation(), count_entity());

			embedding_topic_comp.resize(n_topic);
			embedding_relation.resize(count_relation());

			for(auto i=embedding_relation.begin(); i!=embedding_relation.end(); ++i)
			{
				*i = zeros(n_topic, 1);
			}

			for(auto j=0; j<count_relation(); ++j)
			{
				for(auto i=0; i<count_entity(); ++i)
				{
					embedding_relation[j] += embedding_head(j, i) + embedding_tail(j, i);
				}		
			}

			for(auto i=embedding_relation.begin(); i!=embedding_relation.end(); ++i)
			{
				*i /= sum(*i) + 1e-100;
			}
	}

public:
	void normalize_to_prob()
	{
		for(auto i=0; i<n_topic; ++i)
		{
			double total_sum = 0;
			for(auto item=embedding_head.begin(); item!=embedding_head.end(); ++item)
			{
				total_sum += (*item)(i);
			}

			if (total_sum > 1e-100)
			{
				for(auto item=embedding_head.begin(); item!=embedding_head.end(); ++item)
				{
					(*item)(i) /= total_sum;
				}
			}
		}

		for(auto i=0; i<n_topic; ++i)
		{
			double total_sum = 0;
			for(auto item=embedding_tail.begin(); item!=embedding_tail.end(); ++item)
			{
				total_sum += (*item)(i);
			}

			if (total_sum > 1e-100)
			{
				for(auto item=embedding_tail.begin(); item!=embedding_tail.end(); ++item)
				{
					(*item)(i) /= total_sum;
				}
			}
		}

		for(auto item=embedding_head.begin(); item!=embedding_head.end(); ++item)
		{
			*item += 0.01/(count_entity()*count_relation());
		}

		for(auto item=embedding_tail.begin(); item!=embedding_tail.end(); ++item)
		{
			*item += 0.01/(count_entity()*count_relation());
		}

		embedding_topic /= sum(embedding_topic);
	}

	virtual double prob_triplets(const pair<pair<int, int>,int>& triplet)
	{
		return sum(
			embedding_head(triplet.second, triplet.first.first) 
			% embedding_tail(triplet.second, triplet.first.second)
			% embedding_topic / embedding_relation[triplet.second]) + 1e-100;
	}

	virtual void train_triplet(const pair<pair<int, int>,int>& triplet)
	{
		embedding_head_comp(triplet.second, triplet.first.first) += 
			embedding_head(triplet.second, triplet.first.first) 
			% embedding_topic / prob_triplets(triplet);
		embedding_tail_comp(triplet.second, triplet.first.second) += 
			embedding_tail(triplet.second, triplet.first.second)
			% embedding_topic / prob_triplets(triplet);	
		embedding_topic_comp += embedding_topic / prob_triplets(triplet);
	}

	virtual void train(bool last_time = false)
	{
		for(auto i=embedding_head_comp.begin(); i!=embedding_head_comp.end(); ++i)
		{
			*i = zeros(n_topic, 1);
		}

		for(auto i=embedding_tail_comp.begin(); i!=embedding_tail_comp.end(); ++i)
		{
			*i = zeros(n_topic, 1);
		}

		embedding_topic_comp = zeros(n_topic, 1);

		Model::train(last_time);

		embedding_head = embedding_head_comp;
		embedding_tail = embedding_tail_comp;
		embedding_topic = embedding_topic_comp;

		normalize_to_prob();

		for(auto i=embedding_relation.begin(); i!=embedding_relation.end(); ++i)
		{
			*i = zeros(n_topic, 1);
		}

		for(auto j=0; j<count_relation(); ++j)
		{
			for(auto i=0; i<count_entity(); ++i)
			{
				embedding_relation[j] += embedding_tail(j, i) + embedding_head(j, i);
			}		
		}

		for(auto i=embedding_relation.begin(); i!=embedding_relation.end(); ++i)
		{
			*i /= sum(*i) + 1e-100;
		}

		cout<<embedding_topic.t();
	}
};

class PropergationModel
	:public Model
{
protected:
	vector<vec>		relation_in;
	vector<vec>		relation_out;
	vector<vec>		entity;
	const int		dim;
	const double	alpha;
	const double	training_threshold;

public:
	PropergationModel(		
		const Dataset& dataset,
		const TaskType& task_type,
		const string& logging_base_path,
		int dim,
		double alpha,
		double training_threshold)
		:Model(dataset, task_type, logging_base_path), 
		dim(dim), alpha(alpha), training_threshold(training_threshold)
	{
		logging.record()<<"\t[Dimension]\t"<<dim;
		logging.record()<<"\t[Learning Rate]\t"<<alpha;
		logging.record()<<"\t[Training Threshold]\t"<<training_threshold;
		logging.record()<<"\t[Name]\tPropergation Model";

		relation_in.resize(count_relation());
		for_each(relation_in.begin(), relation_in.end(), 
			[&](vec & elem){elem = (2*randu(dim,1)-1)*sqrt(6.0/dim);});

		relation_out.resize(count_relation());
		for_each(relation_out.begin(), relation_out.end(), 
			[&](vec & elem){elem = ones(dim);  (2 * randu(dim, 1) - 1)*sqrt(6.0 / dim); });

		entity.resize(count_entity());
		for_each(entity.begin(), entity.end(), 
			[&](vec & elem){elem = (2*randu(dim,1)-1)*sqrt(6.0/dim);});
	}

	virtual double prob_triplets(const pair<pair<int, int>,int>& triplet)
	{
		double score = sum(abs(
			entity[triplet.first.first] % relation_in[triplet.second] 
			- entity[triplet.first.second] % relation_out[triplet.second]));
		
		return - score;
	}

	virtual void train_triplet(const pair<pair<int, int>,int>& triplet)
	{
		pair<pair<int, int>,int> triplet_f;
		data_model.sample_false_triplet(triplet, triplet_f);

		if (prob_triplets(triplet) - prob_triplets(triplet_f) > training_threshold)
			return;

		vec factor_vec = - sign(entity[triplet.first.first] % relation_in[triplet.second] 
		- entity[triplet.first.second] % relation_out[triplet.second]);

		entity[triplet.first.first] += 
			alpha * factor_vec % relation_in[triplet.second];
		relation_in[triplet.second] +=
			alpha * factor_vec % entity[triplet.first.first];
		entity[triplet.first.second] -= 
			alpha * factor_vec % relation_out[triplet.second];
		//relation_out[triplet.second] -=
		//	alpha * factor_vec % entity[triplet.first.second];

		factor_vec = - sign(entity[triplet_f.first.first] % relation_in[triplet_f.second] 
		- entity[triplet_f.first.second] % relation_out[triplet_f.second]);

		entity[triplet_f.first.first] -= 
			alpha * factor_vec % relation_in[triplet_f.second];
		relation_in[triplet_f.second] -=
			alpha * factor_vec % entity[triplet_f.first.first];
		entity[triplet_f.first.second] += 
			alpha * factor_vec % relation_out[triplet_f.second];
		//relation_out[triplet_f.second] +=
		//	alpha * factor_vec % entity[triplet_f.first.second];

		if (norm_L2(entity[triplet.first.first]) > 1)
			entity[triplet.first.first] = normalise(entity[triplet.first.first]);
		if (norm_L2(entity[triplet.first.second]) > 1)
			entity[triplet.first.second] = normalise(entity[triplet.first.second]);
		if (norm_L2(entity[triplet_f.first.first]) > 1)
			entity[triplet_f.first.first] = normalise(entity[triplet_f.first.first]);
		if (norm_L2(entity[triplet_f.first.second]) > 1)
			entity[triplet_f.first.second] = normalise(entity[triplet_f.first.second]);

		relation_in[triplet.second] = normalise(relation_in[triplet.second]);
		relation_out[triplet.second] = normalise(relation_out[triplet.second]);
	}
};

class FactorEKL
	:public Model
{
protected:
	vector<vec>	embedding_entity;
	vector<vec>	embedding_relation_head;
	vector<vec>	embedding_relation_tail;
	vec			prob_head;
	vec			prob_tail;

protected:
	vec			acc_prob_head;
	vec			acc_prob_tail;

public:
	const double	margin;
	const double	alpha;
	const double	smoothing;
	const int		dim;

public:
	FactorEKL(
		const Dataset& dataset,
		const TaskType& task_type,
		const string& logging_base_path,
		int dim,
		double alpha,
		double training_threshold,
		double smoothing)
		:Model(dataset, task_type, logging_base_path),
		dim(dim), alpha(alpha), margin(training_threshold), smoothing(smoothing)
	{
		logging.record() << "\t[Name]\tFactorE";
		logging.record() << "\t[Dimension]\t" << dim;
		logging.record() << "\t[Learning Rate]\t" << alpha;
		logging.record() << "\t[Training Threshold]\t" << training_threshold;
		logging.record() << "\t[Smoothing]\t" << smoothing;

		embedding_entity.resize(count_entity());
		for_each(embedding_entity.begin(), embedding_entity.end(),
			[=](vec& elem){elem = normalise(randu(dim), 1); });

		embedding_relation_head.resize(count_relation());
		for_each(embedding_relation_head.begin(), embedding_relation_head.end(),
			[=](vec& elem){elem = normalise(randu(dim), 1); });

		embedding_relation_tail.resize(count_relation());
		for_each(embedding_relation_tail.begin(), embedding_relation_tail.end(),
			[=](vec& elem){elem = normalise(randu(dim), 1); });

		prob_head = normalise(ones(dim));
		prob_tail = normalise(ones(dim));
	}

public:
	virtual double prob_triplets(const pair<pair<int, int>, int>& triplet) override
	{
		vec& head = embedding_entity[triplet.first.first];
		vec& tail = embedding_entity[triplet.first.second];
		vec& relation_head = embedding_relation_head[triplet.second];
		vec& relation_tail = embedding_relation_tail[triplet.second];

		vec head_feature = max(head % relation_head, smoothing * ones(dim));
		vec tail_feature = max(tail % relation_tail, smoothing * ones(dim));

		return - sum(head_feature % log(head_feature / tail_feature));
	}

	virtual void train_derv(const pair<pair<int, int>, int>& triplet, const double alpha) = 0;
};

class FactorE
	:public FactorEKL
{
protected:
	const double sigma;

public:
	FactorE(
		const Dataset& dataset,
		const TaskType& task_type,
		const string& logging_base_path,
		int dim,
		double alpha,
		double training_threshold,
		double sigma)
		:FactorEKL(dataset, task_type, logging_base_path, dim, alpha, training_threshold, 0.0),
		sigma(sigma)
	{
		logging.record() << "\t[Sigma]\t" << sigma;
	}

public:
	virtual double prob_triplets(const pair<pair<int, int>, int>& triplet) override
	{
		vec& head = embedding_entity[triplet.first.first];
		vec& tail = embedding_entity[triplet.first.second];
		vec& relation_head = embedding_relation_head[triplet.second];
		vec& relation_tail = embedding_relation_tail[triplet.second];

		vec head_feature = head % relation_head;
		vec tail_feature = tail % relation_tail;

		return sum(head_feature) * sum(tail_feature)* 
			exp(-sum(abs(head_feature - tail_feature)) / sigma);
	}

	virtual void train_derv(const pair<pair<int, int>, int>& triplet, const double alpha) override
	{
		vec& head = embedding_entity[triplet.first.first];
		vec& tail = embedding_entity[triplet.first.second];
		vec& relation_head = embedding_relation_head[triplet.second];
		vec& relation_tail = embedding_relation_tail[triplet.second];

		vec head_feature = head % relation_head;
		vec tail_feature = tail % relation_tail;
		vec grad = - sign(head_feature - tail_feature) / sigma;

		head += alpha * grad % relation_head
			+alpha * relation_head / sum(head_feature);
		relation_head += alpha * grad % head
			+ alpha * head / sum(head_feature);
		tail += -alpha * grad % relation_tail
			+ alpha * tail_feature / tail / sum(tail_feature);
		relation_tail += -alpha * grad % tail
			+ alpha * tail_feature / relation_tail / sum(tail_feature);

		//relation_head += alpha * pow(alpha, 2) * sign(relation_head - relation_tail);
		//relation_tail -= alpha * pow(alpha, 2) * sign(relation_head - relation_tail);

		head = normalise(max(head, ones(dim) / pow(dim, 5)), 2);
		tail = normalise(max(tail, ones(dim) / pow(dim, 5)), 2);
		relation_head = normalise(max(relation_head, ones(dim) / pow(dim, 5)), 2);
		relation_tail = normalise(max(relation_tail, ones(dim) / pow(dim, 5)), 2);
	}

public:
	virtual void train_triplet(const pair<pair<int, int>, int>& triplet) override
	{
		pair<pair<int, int>, int> triplet_f;
		data_model.sample_false_triplet(triplet, triplet_f);

		if (prob_triplets(triplet) / prob_triplets(triplet_f) > exp(margin/sigma))
			return;

		train_derv(triplet, alpha);
		train_derv(triplet_f, -alpha);
	}
};

class SFactorE
{
protected:
	vector<vec>	embedding_entity;
	vector<vec>	embedding_relation_head;
	vector<vec>	embedding_relation_tail;

public:
	const int		dim;
	const double	sigma;

public:
	SFactorE(int dim, int entity_count, int relation_count, double sigma)
		:dim(dim), sigma(sigma)
	{
		embedding_entity.resize(entity_count);
		for_each(embedding_entity.begin(), embedding_entity.end(),
			[=](vec& elem){elem = normalise(randu(dim), 2); });

		embedding_relation_head.resize(relation_count);
		for_each(embedding_relation_head.begin(), embedding_relation_head.end(),
			[=](vec& elem){elem = normalise(ones(dim), 2); });

		embedding_relation_tail.resize(relation_count);
		for_each(embedding_relation_tail.begin(), embedding_relation_tail.end(),
			[=](vec& elem){elem = normalise(ones(dim), 2); });
	}

	double prob(const pair<pair<int, int>, int>& triplet)
	{
		vec& head = embedding_entity[triplet.first.first];
		vec& tail = embedding_entity[triplet.first.second];
		vec& relation_head = embedding_relation_head[triplet.second];
		vec& relation_tail = embedding_relation_tail[triplet.second];

		vec head_feature = head % relation_head;
		vec tail_feature = tail % relation_tail;

		return sum(head_feature) * sum(tail_feature)
			* exp(-sum(abs(head_feature - tail_feature))/sigma);
	}

	void train(const pair<pair<int, int>, int>& triplet, const double alpha)
	{
		vec& head = embedding_entity[triplet.first.first];
		vec& tail = embedding_entity[triplet.first.second];
		vec& relation_head = embedding_relation_head[triplet.second];
		vec& relation_tail = embedding_relation_tail[triplet.second];

		vec head_feature = head % relation_head;
		vec tail_feature = tail % relation_tail;
		vec grad = -sign(head_feature - tail_feature) / sigma;

		head += alpha * grad % relation_head
			+ alpha * relation_head / sum(head_feature);
		relation_head += alpha * grad % head
			+ alpha * head / sum(head_feature);
		tail += -alpha * grad % relation_tail
			+ alpha * tail_feature / tail / sum(tail_feature);
		relation_tail += -alpha * grad % tail
			+ alpha * tail_feature / relation_tail / sum(tail_feature);

		//relation_head += alpha * pow(alpha, 2) * sign(relation_head - relation_tail);
		//relation_tail -= alpha * pow(alpha, 2) * sign(relation_head - relation_tail);

		head = normalise(max(head, ones(dim) / pow(dim, 5)), 2);
		tail = normalise(max(tail, ones(dim) / pow(dim, 5)), 2);
		relation_head = normalise(max(relation_head, ones(dim) / pow(dim, 5)), 2);
		relation_tail = normalise(max(relation_tail, ones(dim) / pow(dim, 5)), 2);
	}
};

class MFactorE
	:public Model
{
protected:
	vector<SFactorE*>	factors;
	vector<vec>			relation_space;

protected:
	vector<vec>			acc_space;

public:
	const double	margin;
	const double	alpha;
	const int		dim;
	const int		n_factor;
	const double	sigma;

public:
	MFactorE(
		const Dataset& dataset,
		const TaskType& task_type,
		const string& logging_base_path,
		int dim,
		double alpha,
		double training_threshold,
		double sigma,
		int n_factor)
		:Model(dataset, task_type, logging_base_path),
		dim(dim), alpha(alpha), margin(training_threshold), n_factor(n_factor), sigma(sigma)
	{
		logging.record() << "\t[Name]\tFactorE";
		logging.record() << "\t[Dimension]\t" << dim;
		logging.record() << "\t[Learning Rate]\t" << alpha;
		logging.record() << "\t[Training Threshold]\t" << training_threshold;
		logging.record() << "\t[Factor Number]\t" << n_factor;

		relation_space.resize(count_entity());
		for (vec& elem : relation_space)
		{
			elem = normalise(randu(n_factor));
		}

		for (auto i = 0; i < n_factor; ++i)
		{
			factors.push_back(new SFactorE(dim, count_entity(), count_relation(), sigma));
		}
	}

	vec get_error_vec(const pair<pair<int, int>, int>& triplet)
	{
		vec score(n_factor);
		auto i_score = score.begin();
		for (auto factor : factors)
		{
			*i_score++ = factor->prob(triplet);
		}

		return score;
	}

	virtual double prob_triplets(const pair<pair<int, int>, int>& triplet) override
	{
		return prod(get_error_vec(triplet));
	}

	virtual void train_triplet(const pair<pair<int, int>, int>& triplet) override
	{
		pair<pair<int, int>, int> triplet_f;
		data_model.sample_false_triplet(triplet, triplet_f);

		if (prob_triplets(triplet) / prob_triplets(triplet_f) > exp(n_factor * margin/sigma))
			return;

		vec ef = get_error_vec(triplet);

		auto i = relation_space[triplet.second].begin();
		for (auto i=0; i<n_factor; ++i)
		{
			factors[i]->train(triplet, alpha);
			factors[i]->train(triplet_f, -alpha);

			++i;
		}
	}

	virtual void train(bool last_time = false) override
	{
		Model::train(last_time);
	}
};