﻿#include "ParametricLSystem.h"
#include <iostream>
#include <time.h>
#include <algorithm>
#include "MLUtils.h"
#include "GLUtils.h"
#include <list>
#include <boost/thread.hpp>
#include <boost/bind.hpp>

#define SQR(x)	((x) * (x))

namespace parametriclsystem {

const double M_PI = 3.141592653592;

Literal::Literal(const string& name, int depth, bool param_defined) {
	this->name = name;
	this->depth = depth;
	this->param_value1 = 0.0;
	this->param_value2 = 0.0;
	this->param_defined = param_defined;
}

Literal::Literal(const string& name, int depth, double param_value) {
	this->name = name;
	this->depth = depth;
	this->param_value1 = param_value;
	this->param_value2 = 0.0;
	this->param_defined = true;
}

Literal::Literal(const string& name, int depth, double param_value1, double param_value2) {
	this->name = name;
	this->depth = depth;
	this->param_value1 = param_value1;
	this->param_value2 = param_value2;
	this->param_defined = true;
}

String Literal::operator+(const Literal& l) const {
	String ret = *this;
	return ret + l;
}

int Literal::type() {
	if (name == "F" || name == "f" || name == "C" || name == "[" || name == "]" || name == "+" || name == "-" || name == "\\" || name == "/" || name == "&" || name == "^" || name == "#") {
		return TYPE_TERMINAL;
	} else {
		return TYPE_NONTERMINAL;
	}
}

String::String() {
	this->cursor = -1;
}

String::String(const string& str, int depth) {
	this->str.push_back(Literal(str, depth));
	this->cursor = 0;
}

String::String(const Literal& l) {
	this->str.push_back(l);
	this->cursor = 0;
}

void String::operator+=(const Literal& l) {
	str.push_back(l);

	if (cursor < 0) cursor = 0;
}

void String::operator+=(const String& str) {
	for (int i = 0; i < str.length(); ++i) {
		this->str.push_back(str[i]);
	}

	if (cursor < 0) cursor = 0;
}

String String::operator+(const String& str) const {
	String new_str = *this;

	for (int i = 0; i < str.length(); ++i) {
		new_str.str.push_back(str[i]);
	}

	if (new_str.cursor < 0 && new_str.length() > 0) new_str.cursor = 0;

	return new_str;
}

void String::setValue(double value) {
	str[cursor].param_value1 = value;
	str[cursor].param_defined = true;

	cursor++;

	// 次のリテラルを探す
	nextCursor(str[cursor].depth);
}

void String::replace(const String& str) {
	int depth = this->str[cursor].depth;

	this->str.erase(this->str.begin() + cursor);
	this->str.insert(this->str.begin() + cursor, str.str.begin(), str.str.end());

	// depthの設定
	for (int i = 0; i < str.length(); ++i) {
		this->str[cursor + i].depth = depth + 1;
	}

	// 次のリテラルを探す
	nextCursor(depth);
}

String String::getExpand() const {
	String ret;

	int nest = 0;
	for (int i = cursor; i < str.size(); ++i) {
		if (str[i].name == "[") {
			nest++;
		} else if (str[i].name == "]") {
			nest--;
		}

		if (nest < 0) break;

		ret += str[i];
	}

	return ret;
}

void String::nextCursor(int depth) {
	for (int i = cursor; i < str.size(); ++i) {
		if (str[i].depth != depth) continue;

		if (str[i].type() == Literal::TYPE_NONTERMINAL) {
			cursor = i;
			return;
		} else if (str[i].type() == Literal::TYPE_TERMINAL && !str[i].param_defined) {
			cursor = i;
			return;
		}
	}

	// 同じdepthでリテラルが見つからない場合は、depth+1にする
	depth++;

	for (int i = 0; i < str.size(); ++i) {
		if (str[i].depth != depth) continue;

		if (str[i].type() == Literal::TYPE_NONTERMINAL) {
			cursor = i;
			return;
		} else if (str[i].type() == Literal::TYPE_TERMINAL && !str[i].param_defined) {
			cursor = i;
			return;
		}
	}

	// リテラルが見つからない場合は、-1にする
	cursor = -1;
}

void String::rewrite(const Action& action) {
	if (action.type == Action::ACTION_RULE) {
		replace(action.rule);
	} else {
		setValue(action.value);
	}
}

ostream& operator<<(ostream& os, const String& str) {
	os << setprecision(1);
	for (int i = 0; i < str.length(); ++i) {
		os << str[i].name;
	}

    return os;
}

Action::Action(int index, const String& rule) {
	this->type = ACTION_RULE;
	this->index = index;
	this->rule = rule;
}

Action::Action(int index, double value) {
	this->type = ACTION_VALUE;
	this->index = index;
	this->value = value;
}

ostream& operator<<(ostream& os, const Action& a) {
	os << "type(";
	if (a.type == Action::ACTION_RULE) {
		os << "Rule" << "): " << a.rule;
	} else {
		os << "Value" << "): " << a.value;
	}

	return os;
}

Node::Node(const String& model) {
	this->parent = NULL;
	this->model = model;
	best_score = -std::numeric_limits<double>::max();
	visits = 0;
	fixed = false;
}

Node::Node(const String& model, const Action& action) {
	this->parent = NULL;
	this->model = model;
	this->action = action;
	best_score = -std::numeric_limits<double>::max();
	visits = 0;
	fixed = false;
}

/**
 * ノードに、指定されたmodelの子ノードを追加する。
 */
Node* Node::addChild(const String& model, const Action& action) {
	Node* child = new Node(model, action);
	children.push_back(child);
	child->parent = this;
	return child;
}

/**
 * ノードに、actionを登録する。
 * また、untriedActionsに、登録したactionのindex番号のリストをセットする
 *
 * @param actions	actionのリスト
 */
void Node::setActions(std::vector<Action>& actions) {
	this->actions = actions;
	for (int i = 0; i < actions.size(); ++i) {
		untriedActions.push_back(i);
	}
}

/**
 * untriedActionsから1つをランダムに選択して返却する。
 * 選択されたactionは、リストから削除される。
 *
 * @return		選択されたaction
 */
Action Node::randomlySelectAction() {
	int index = ml::genRand(0, untriedActions.size());
	Action action = actions[untriedActions[index]];
	untriedActions.erase(untriedActions.begin() + index);
	return action;
}

/**
 * UCTアルゴリズムに従い、子ノードを1つ選択する。
 */
Node* Node::UCTSelectChild() {
	double max_uct = -std::numeric_limits<double>::max();
	Node* best_child = NULL;

	for (int i = 0; i < children.size(); ++i) {
		// スコアが確定済みの子ノードは探索対象外とする
		if (children[i]->fixed) continue;

		double uct;
		if (children[i]->visits == 0) {
			uct = 10000 + ml::genRand(0, 1000);
		} else {
			uct = children[i]->best_score
				+ PARAM_EXPLORATION * sqrt(2 * log((double)visits) / (double)children[i]->visits)
				+ PARAM_EXPLORATION_VARIANCE * sqrt(ml::variance(children[i]->scores) + 0.0 / (double)children[i]->visits);
		}

		if (uct > max_uct) {
			max_uct = uct;
			best_child = children[i];
		}
	}

	return best_child;
}

/**
 * ベストスコアの子ノードを返却する。
 *
 * @return		ベストスコアの子ノード
 */
Node* Node::bestChild() {
	double best_score = -std::numeric_limits<double>::max();
	Node* best_child = NULL;

	for (int i = 0; i < children.size(); ++i) {
		if (children[i]->best_score > best_score) {
			best_score = children[i]->best_score;
			best_child = children[i];
		}
	}

	return best_child;
}

ParametricLSystem::ParametricLSystem(const String& axiom) {
	N_THREADS = 1;
	MAX_ITERATIONS = 500;//1000;
	MAX_ITERATIONS_FOR_MC = 10;
	NUM_MONTE_CARLO_SAMPLING = 100;
	MASK_RADIUS_RATIO = 0.11;

	this->axiom = axiom;

	initActionsTemplate();
	timer.validate(false);
}

void ParametricLSystem::initActionsTemplate() {
	{
		String rule1 = Literal("#", 0)
			+ Literal("F", 0, 0, 0)
			+ Literal("\\", 0)
			+ Literal("X", 0, 0, 0);
		actions_template["X1"].push_back(Action(0, rule1));

		String rule2 = Literal("#", 0)
			+ Literal("F", 0, 0, 0)
			+ Literal("[", 0, true)
			+ Literal("+", 0)
			+ Literal("X", 0, 0, 0)
			+ Literal("]", 0, true)
			+ Literal("F", 0, 0, 0)
			+ Literal("\\", 0)
			+ Literal("X", 0, 0, 0);
		actions_template["X1"].push_back(Action(1, rule2));
	}

	{
		String rule1;
		actions_template["X2"].push_back(Action(0, rule1));

		String rule2 = Literal("#", 0)
			+ Literal("F", 0, 0, 0)
			+ Literal("\\", 0)
			+ Literal("X", 0, 0, 0);
		actions_template["X2"].push_back(Action(1, rule2));

		String rule3 = Literal("#", 0)
			+ Literal("F", 0, 0, 0)
			+ Literal("[", 0, true)
			+ Literal("+", 0)
			+ Literal("X", 0, 0, 0)
			+ Literal("]", 0, true)
			+ Literal("F", 0, 0, 0)
			+ Literal("\\", 0)
			+ Literal("X", 0, 0, 0);
		actions_template["X2"].push_back(Action(2, rule3));

		String rule4 = Literal("F", 0, 0, 0)
			+ Literal("[", 0, true)
			+ Literal("+", 0)
			+ Literal("Y", 0, 3.0, 100.0)
			+ Literal("]", 0, true)
			+ Literal("F", 0, 0, 0)
			+ Literal("#", 0)
			+ Literal("\\", 0, 50.0)
			+ Literal("X", 0, 0, 0);
		actions_template["X2"].push_back(Action(3, rule4));
	}

	{
		String rule1;
		actions_template["X3"].push_back(Action(0, rule1));

		String rule2 = Literal("F", 0, 0, 0)
			+ Literal("[", 0, true)
			+ Literal("+", 0)
			+ Literal("Y", 0, 3.0, 100.0)
			+ Literal("]", 0, true)
			+ Literal("F", 0, 0, 0)
			+ Literal("#", 0)
			+ Literal("\\", 0, 50.0)
			+ Literal("X", 0, 0, 0);
		actions_template["X3"].push_back(Action(1, rule2));
	}

	{
		String rule1;
		actions_template["Y"].push_back(Action(0, rule1));
		
		String rule2 = Literal("F", 0, 2.0, 250.0)
			+ Literal("[", 0, true)
			+ Literal("+", 0, 60.0)
			+ Literal("F", 0, 1.0, 250.0)
			+ Literal("\\", 0, 70.0)
			+ Literal("C", 0, 3.0, 2.0)
			+ Literal("]", 0, true)
			+ Literal("[", 0, true)
			+ Literal("-", 0, 60.0)
			+ Literal("F", 0, 1.0, 250.0)
			+ Literal("\\", 0, -70.0)
			+ Literal("C", 0, 3.0, 2.0)
			+ Literal("]", 0, true)
			+ Literal("F", 0, 2.0, 250.0)
			+ Literal("#", 0)
			+ Literal("\\", 0, 50.0)
			+ Literal("Y", 0, 4.0, 250.0);
		actions_template["Y"].push_back(Action(1, rule2));
	}

	{
		int count = 0;
		for (int k = -80; k <= 80; k += 20) {
			if (k == 0) continue;
			actions_template["-"].push_back(Action(count, k));
			actions_template["+"].push_back(Action(count++, k));
		}
	}

	{
		int count = 0;
		for (int k = -30; k <= 30; k += 10, ++count) {
			actions_template["#"].push_back(Action(count, k));
		}
	}

	{
		int count = 0;
		for (int k = 10; k <= 50; k += 10, ++count) {
			actions_template["\\"].push_back(Action(count, k));
		}
	}
}

/**
 * 適当な乱数シードに基づいて、ランダムにgenerateする。
 *
 * @param random_seed		乱数シード
 * @return					生成されたモデル
 */
String ParametricLSystem::derive(int random_seed) {
	String result_model;
	ml::initRand(random_seed);
	while (true) {
		result_model = derive(axiom, MAX_ITERATIONS);
		if (result_model.length() > 0) break;
	}

	return result_model;
}

/**
 * 指定された開始モデルからスタートし、ランダムにgenerateする。
 *
 * @param start_model			開始モデル
 * @param max_iterations		繰り返し数
 * @param build_tree			木を生成するか？
 * @param indicator [OUT]		生成されたモデルのindicator
 * @return						生成されたモデル
 */
String ParametricLSystem::derive(const String& start_model, int max_iterations) {
	String model = start_model;

	for (int iter = 0; iter < max_iterations; ++iter) {
		timer.start("get_actions");
		std::vector<Action> actions = getActions(model);
		timer.end("get_actions");
		if (actions.size() == 0) break;
		
		timer.start("action_apply");
		int index = ml::genRand(0, actions.size());
		model.rewrite(actions[index]);
		timer.end("action_apply");
	}

	return model;
}

void ParametricLSystem::draw(const String& model, RenderManager* renderManager) {
	std::vector<Vertex> vertices;

	glm::mat4 modelMat;

	std::list<glm::mat4> stack;

	int undefined = 0;
	for (int i = 0; i < model.length(); ++i) {
		if (undefined == 0 && !model[i].param_defined) {
			undefined = 1;
			continue;
		}

		if (model[i].name == "[") {
			stack.push_back(modelMat);
			if (undefined > 0) undefined++;
		} else if (model[i].name == "]") {
			if (!stack.empty()) {
				modelMat = stack.back();
				stack.pop_back();
				if (undefined > 0) undefined--;
			}
		} else if (undefined > 0) {
			continue;
		} else if (model[i].name == "+") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "-") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "#") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "\\") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 1, 0));
		} else if (model[i].name == "/") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(0, 1, 0));
		} else if (model[i].name == "&") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(1, 0, 0));
		} else if (model[i].name == "^") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(1, 0, 0));
		} else if (model[i].name == "f") {
			modelMat = glm::translate(modelMat, glm::vec3(0, model[i].param_value1, 0));
		} else if (model[i].name == "F") {
			double length = model[i].param_value1;
			double radius1 = INITIAL_SIZE * exp(-SIZE_ATTENUATION * model[i].param_value2);
			double radius2 = INITIAL_SIZE * exp(-SIZE_ATTENUATION * (model[i].param_value2 + model[i].param_value1));
			
			// 線を描画する
			glutils::drawCylinderY(radius1, radius2, length, glm::vec3(0.9, 0.3, 0.3), modelMat, vertices);
			modelMat = glm::translate(modelMat, glm::vec3(0, length, 0));
			glutils::drawSphere(radius2, glm::vec3(0.9, 0.3, 0.3), modelMat, vertices);
		} else if (model[i].name == "C") {
			double length = model[i].param_value1;
			double radius = model[i].param_value2;
			
			glm::mat4 mat = glm::translate(modelMat, glm::vec3(0, length, 0));

			// 円を描画する
			glutils::drawCircle(radius, length, glm::vec3(0.3, 1, 0.3), mat, vertices);
		}
	}
	
	renderManager->removeObject("object");
	renderManager->addObject("object", "", vertices);
}

/**
 * 指定された文字列に基づいてモデルを生成し、indicatorを計算して返却する。
 * 
 * @param model				モデルを表す文字列
 * @param mvpMat			カメラ視点への射影行列
 * @param indicator [OUT]	indicator
 */
inline void ParametricLSystem::computeIndicator(const String& model, const glm::mat4& baseModelMat, std::vector<cv::Mat>& indicator) {
	computeIndicator(model, baseModelMat, cv::Rect(0, 0, GRID_SIZE, GRID_SIZE), indicator);
}

/**
 * 指定された文字列に基づいてモデルを生成し、indicatorを計算して返却する。
 * 
 * @param model				モデルを表す文字列
 * @param mvpMat			カメラ視点への射影行列
 * @param baseModelMat		モデルのベース変換行列
 * @param indicator [OUT]	indicator
 */
inline void ParametricLSystem::computeIndicator(const String& model, const glm::mat4& baseModelMat, const cv::Rect& roi, std::vector<cv::Mat>& indicator) {
	indicator.resize(NUM_LAYERS);
	for (int i = 0; i < NUM_LAYERS; ++i) {
		indicator[i] = cv::Mat::zeros(roi.height, roi.width, CV_32F);
	}

	std::list<glm::mat4> stack;

	glm::mat4 modelMat = baseModelMat;

	int undefined = 0;
	for (int i = 0; i < model.length(); ++i) {
		if (undefined == 0 && !model[i].param_defined) {
			undefined = 1;
			continue;
		}

		if (model[i].name == "[") {
			stack.push_back(modelMat);
			if (undefined > 0) undefined++;
		} else if (model[i].name == "]") {
			if (!stack.empty()) {
				modelMat = stack.back();
				stack.pop_back();
				if (undefined > 0) undefined--;
			}
		} else if (undefined > 0) {
			continue;
		} else if (model[i].name == "+") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "-") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "#") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "\\") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 1, 0));
		} else if (model[i].name == "/") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(0, 1, 0));
		} else if (model[i].name == "&") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(1, 0, 0));
		} else if (model[i].name == "^") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(1, 0, 0));
		} else if (model[i].name == "f") {
			modelMat = glm::translate(modelMat, glm::vec3(0, model[i].param_value1, 0));
		} else if (model[i].name == "F") {
			double length = model[i].param_value1;
			double radius1 = INITIAL_SIZE * exp(-SIZE_ATTENUATION * model[i].param_value2);
			double radius2 = INITIAL_SIZE * exp(-SIZE_ATTENUATION * (model[i].param_value2 + model[i].param_value1));
			double radius = (radius1 + radius2) * 0.5f;

			// 線を描画する代わりに、indicatorを更新する
			glm::vec4 p1(0, 0, 0, 1);
			glm::vec4 p2(0, length, 0, 1);
			p1 = modelMat * p1;
			p2 = modelMat * p2;
			int u1 = (p1.x / p1.w + 1.0) * GRID_SIZE * 0.5 - roi.x;
			int v1 = (p1.y / p1.w + 1.0) * GRID_SIZE * 0.5 - roi.y;
			int u2 = (p2.x / p2.w + 1.0) * GRID_SIZE * 0.5 - roi.x;
			int v2 = (p2.y / p2.w + 1.0) * GRID_SIZE * 0.5 - roi.y;

			int thickness = max(1.0, radius);
			cv::line(indicator[0], cv::Point(u1, v1), cv::Point(u2, v2), cv::Scalar(1), thickness);

			modelMat = glm::translate(modelMat, glm::vec3(0, length, 0));
		} else if (model[i].name == "C") {
			double length = model[i].param_value1;
			double radius = model[i].param_value2;

			// 描画する代わりに、indicatorを更新する
			glm::vec4 p1(0, length, 0, 1);
			glm::vec4 p2(radius, length, 0, 1);
			glm::vec4 p3(0, length * 2.0, 0, 1);
			p1 = modelMat * p1;
			p2 = modelMat * p2;
			p3 = modelMat * p3;
			double u1 = (p1.x / p1.w + 1.0) * GRID_SIZE * 0.5 - roi.x;
			double v1 = (p1.y / p1.w + 1.0) * GRID_SIZE * 0.5 - roi.y;
			double u2 = (p2.x / p2.w + 1.0) * GRID_SIZE * 0.5 - roi.x;
			double v2 = (p2.y / p2.w + 1.0) * GRID_SIZE * 0.5 - roi.y;
			double u3 = (p3.x / p3.w + 1.0) * GRID_SIZE * 0.5 - roi.x;
			double v3 = (p3.y / p3.w + 1.0) * GRID_SIZE * 0.5 - roi.y;
			int r1 = sqrt((u2 - u1) * (u2 - u1) + (v2 - v1) * (v2 - v1));
			int r2 = sqrt((u3 - u1) * (u3 - u1) + (v3 - v1) * (v3 - v1));
			float angle = atan2(v2 - v1, u2 - u1) / M_PI * 180.0;

			//cv::ellipse(indicator[1], cv::Point(u1, v1), cv::Size(r1, r2), angle, 0, 360, cv::Scalar(1), -1);
			cv::rectangle(indicator[1], cv::Point(u1-r1, v1-r2), cv::Point(u1+r1, v1+r2), cv::Scalar(1), -1);
		}
	}
}

/**
 * 指定されたターゲットindicatorに近いモデルを生成する。
 *
 * @param target			ターゲットindicator
 * @param indicator [OUT]	生成されたモデルのindicator
 * @return					生成されたモデル
 */
String ParametricLSystem::inverse(const std::vector<cv::Mat>& target, const glm::mat4& mvpMat) {
	// UCTを使って探索木を構築していく
	String model = axiom;

	for (int l = 0; l < MAX_ITERATIONS; ++l) {
		model = MultiUCT(model, target, mvpMat, l);

		/*std::vector<cv::Mat> indicator;
		computeIndicator(model, mvpMat, indicator);
		double sc = score(indicator, target);*/
		
		/////// デバッグ ///////
		/*
		char filename[256];
		sprintf(filename, "images/indicator_%d.png", l);
		computeIndicator(model, mvpMat, indicator);

		ml::mat_save(filename, indicator[0] + target[0] * 0.4);
		*/
		/////// デバッグ ///////

		//cout << l << ": " << "Best score=" << sc << endl;

		// これ以上、derivationできなら、終了
		if (model.cursor < 0) break;
	}

	// スコア表示
	std::vector<cv::Mat> indicator;
	computeIndicator(model, mvpMat, indicator);
	cout << score(indicator, target) << endl;
	
	return model;
}

String ParametricLSystem::MultiUCT(const String& current_model, const std::vector<cv::Mat>& target, const glm::mat4& mvpMat, int derivation_step) {
	std::vector<boost::thread> th(N_THREADS);
	std::vector<std::pair<String, double> > best_options(N_THREADS);

	for (int i = 0; i < N_THREADS; ++i) {
		th[i] = boost::thread(&ParametricLSystem::UCT, this, current_model, target, mvpMat, i, derivation_step, &best_options[i]);
	}

	double best_score = -std::numeric_limits<double>::max();
	String best_model;
	for (int i = 0; i < N_THREADS; ++i) {
		th[i].join();

		if (best_options[i].second > best_score) {
			best_model = best_options[i].first;
			best_score = best_options[i].second;
		}
	}

	return best_model;
}

/**
 * 指定されたmodelからUCTをスタートし、最善のoptionを返却する。
 *
 * @param current_model		モデル
 * @param target			ターゲット
 * @param mvpMat			model/view/projection行列
 * @return					最善のoption
 */
void ParametricLSystem::UCT(const String& current_model, const std::vector<cv::Mat>& target, const glm::mat4& mvpMat, int rand_seed, int derivation_step, std::pair<String, double>* best_option) {
	// これ以上、derivationできなら、終了
	int index = current_model.cursor;
	if (index < 0) {
		best_option->first = current_model;
		best_option->second = 0;
		return;
	}

	// 乱数初期化
	ml::initRand(rand_seed);

	// 現在のカーソルのdepthを取得
	int depth = current_model[current_model.cursor].depth;

	// 現在の座標を計算
	glm::mat4 baseModelMat;
	glm::vec2 curPt = computeCurrentPoint(current_model, mvpMat, baseModelMat);
	baseModelMat = mvpMat * baseModelMat;

	// cripping領域を計算
	cv::Rect crip_roi(curPt.x - MASK_RADIUS_RATIO * GRID_SIZE, curPt.y - MASK_RADIUS_RATIO * GRID_SIZE, curPt.x + MASK_RADIUS_RATIO * GRID_SIZE, curPt.y + MASK_RADIUS_RATIO * GRID_SIZE);
	if (crip_roi.x < 0) crip_roi.x = 0;
	if (crip_roi.y < 0) crip_roi.y = 0;
	if (crip_roi.width >= GRID_SIZE) crip_roi.width = GRID_SIZE - 1;
	if (crip_roi.height >= GRID_SIZE) crip_roi.height = GRID_SIZE - 1;
	crip_roi.width -= crip_roi.x - 1;
	crip_roi.height -= crip_roi.y - 1;
	
	// targetから現在の座標周辺を切り取る
	std::vector<cv::Mat> cripped_target(target.size());
	{
		for (int i = 0; i < target.size(); ++i) {
			cripped_target[i] = target[i](crip_roi);
		}
	}

	// ベースとなるindicatorを計算
	std::vector<cv::Mat> baseIndicator;
	computeIndicator(current_model, mvpMat, crip_roi, baseIndicator);
	/*
	for (int i = 0; i < NUM_LAYERS; ++i) {
		char filename[256];
		sprintf(filename, "images/base_indicator_%d.png", i);
		ml::mat_save(filename, baseIndicator[i] * 0.7 + cripped_target[i] * 0.3);
	}
	*/

	// expandするリテラルを取得する
	String root_model = current_model.getExpand();

	// ルートノードを作成
	Node* root_node = new Node(root_model);
	root_node->setActions(getActions(root_model));

	for (int iter = 0; iter < NUM_MONTE_CARLO_SAMPLING; ++iter) {
		// もしノードがリーフノードなら、終了
		if (root_node->untriedActions.size() == 0 && root_node->children.size() == 0) break;

		// 現在のノードのスコアが確定したら、終了
		if (root_node->fixed) break;

		Node* node = root_node;

		// 探索木のリーフノードを選択
		timer.start("UCT_select");
		while (node->untriedActions.size() == 0 && node->children.size() > 0) {
			node = node->UCTSelectChild();
		}
		timer.end("UCT_select");
		
		// 子ノードがまだ全てexpandされていない時は、1つランダムにexpand
		timer.start("UCT_expand");
		if (node->untriedActions.size() > 0) {// && node->children.size() <= ml::log((double)iter * 0.01 + 1, 1.4)) {
			Action action = node->randomlySelectAction();
			String child_model = node->model;
			child_model.rewrite(action);
						
			node = node->addChild(child_model, action);
			node->setActions(getActions(child_model));
		}
		timer.end("UCT_expand");

		// ランダムにderiveする
		String result_model = derive(node->model, MAX_ITERATIONS_FOR_MC);

		// indicatorを計算する
		timer.start("compute_indicator");
		std::vector<cv::Mat> indicator;
		computeIndicator(result_model, baseModelMat, crip_roi, indicator);
		for (int i = 0; i < NUM_LAYERS; ++i) {
			indicator[i] += baseIndicator[i];

			// clamp
			cv::threshold(indicator[i], indicator[i], 0.0, 1.0, cv::THRESH_BINARY);
		}
		timer.end("compute_indicator");

		// スコアを計算する
		timer.start("compute_score");
		double sc = score(indicator, cripped_target);
		node->best_score = sc;
		timer.end("compute_score");

		/////// デバッグ ///////
		/*{
			for (int i = 0; i < NUM_LAYERS; ++i) {
				char filename[256];
				sprintf(filename, "images/indicator_%d_%d_%d_%lf.png", derivation_step, iter, i, sc);
				cv::Mat img = indicator[i] * 0.7 + cripped_target[i] * 0.3;

				ml::mat_save(filename, img);

				cout << "   " << filename << " : " << result_model << endl;
			}
		}*/
		/////// デバッグ ///////

		// リーフノードなら、スコアを確定する
		if (node->untriedActions.size() == 0 && node->children.size() == 0) {
			node->fixed = true;
		}

		// スコアをbackpropagateする
		timer.start("UCT_backpropagate");
		Node* leaf = node;
		while (node != NULL) {
			node->visits++;
			node->scores.push_back(sc);
			if (sc > node->best_score) {
				node->best_score = sc;
			}

			// 子ノードが全て展開済みで、且つ、スコア確定済みなら、このノードのスコアも確定とする
			if (node->untriedActions.size() == 0) {
				bool fixed = true;
				for (int c = 0; c < node->children.size(); ++c) {
					if (!node->children[c]->fixed) {
						fixed = false;
						break;
					}
				}
				node->fixed = fixed;
			}

			node = node->parent;
		}
		timer.end("UCT_backpropagate");
	}

	////// デバッグ //////
	/*
	for (int i = 0; i < current_node->children.size(); ++i) {
		cout << "  " << i << ":" << endl;
		if (current_node->children[i]->action.type == Action::ACTION_RULE) {
			cout << "   " << current_node->children[i]->action.rule << endl;
		} else {
			cout << "   " << current_node->children[i]->action.value << endl;
		}
		cout << "    - visits: " << current_node->children[i]->visits << endl;
		cout << "    - best score: " << current_node->children[i]->best_score << endl;
		cout << "    - avg score: " << ml::mean(current_node->children[i]->scores) << endl;
		cout << "    - var score: " << ml::variance(current_node->children[i]->scores) << endl;
	}
	*/
	////// デバッグ //////

	best_option->first = current_model;
	Node* best_child = root_node->bestChild();
	best_option->first.rewrite(best_child->action);
	best_option->second = best_child->best_score;

	/////// デバッグ ///////
	/*
	char filename[256];
	computeIndicator(best_model, 4, bestIndicator);
	sprintf(filename, "images/indicator_%d.png", derivation_step);

	cv::Mat target2;
	cv::resize(target, target2, cv::Size(400, 400));
	cv::Mat img = bestIndicator + target2 * 0.4;
	img = ml::mat_mask(img, mask2, 0.8);

	ml::mat_save(filename, img);
	*/
	/////// デバッグ ///////

	// 探索木のメモリを解放する
	timer.start("release_memory");
	releaseNodeMemory(root_node);
	timer.end("release_memory");
}

/**
 * indicatorのスコアを計算して返却する。
 *
 * @param indicator		indicator
 * @param target		ターゲットindicator
 * @param mask			マスク
 * @return				スコア
 */
inline double ParametricLSystem::score(const std::vector<cv::Mat>& indicator, const std::vector<cv::Mat>& target) {
	double count = 0;
	double total = 0;

	for (int i = 0; i < indicator.size(); ++i) {
		cv::Mat result;
		cv::subtract(indicator[i], target[i], result);
		count += cv::countNonZero(result);

		total += cv::countNonZero(target[i]);
	}

	if (total > 0.0) {
		return 1.0 - count / total;
	} else {
		if (count > 0.0) {
			return -1.0;
		} else {
			return 1.0;
		}
	}
}

/**
 * 指定されたモデルの、次のderivationの候補を返却する。
 * Hardcoding: 各literalは、4つのパラメータを持つ (このセグメントの長さ、このセグメントの最初の半径)
 *
 * @param model		モデル
 * @return			次のderivationの候補リスト
 */
std::vector<Action> ParametricLSystem::getActions(const String& model) {
	std::vector<Action> actions;

	// 展開するパラメータを決定
	int i = model.cursor;

	// 新たなderivationがないなら、終了
	if (i == -1) return actions;

	if (model[i].name == "X") {
		if (model[i].param_value2 <= 40.0) {
			actions = actions_template["X1"];
			actions[0].rule.str[1].param_value1 = model[i].param_value1;
			actions[0].rule.str[1].param_value2 = model[i].param_value2;
			actions[0].rule.str[3].param_value1 = model[i].param_value1 * LENGTH_ATTENUATION;
			actions[0].rule.str[3].param_value2 = model[i].param_value2 + model[i].param_value1;
			actions[1].rule.str[1].param_value1 = model[i].param_value1 * 0.5f;
			actions[1].rule.str[1].param_value2 = model[i].param_value2;
			actions[1].rule.str[4].param_value1 = model[i].param_value1 * LENGTH_ATTENUATION;
			actions[1].rule.str[4].param_value2 = model[i].param_value2 + model[i].param_value1 * 0.5f;
			actions[1].rule.str[6].param_value1 = model[i].param_value1 * 0.5f;
			actions[1].rule.str[6].param_value2 = model[i].param_value2 + model[i].param_value1 * 0.5f;
			actions[1].rule.str[8].param_value1 = model[i].param_value1 * LENGTH_ATTENUATION;
			actions[1].rule.str[8].param_value2 = model[i].param_value2 + model[i].param_value1;
		} else if (model[i].param_value2 < 200.0) {
			actions = actions_template["X2"];
			actions[1].rule.str[1].param_value1 = model[i].param_value1;
			actions[1].rule.str[1].param_value2 = model[i].param_value2;
			actions[1].rule.str[3].param_value1 = model[i].param_value1 * LENGTH_ATTENUATION;
			actions[1].rule.str[3].param_value2 = model[i].param_value2 + model[i].param_value1;
			actions[2].rule.str[1].param_value1 = model[i].param_value1 * 0.5f;
			actions[2].rule.str[1].param_value2 = model[i].param_value2;
			actions[2].rule.str[4].param_value1 = model[i].param_value1 * LENGTH_ATTENUATION;
			actions[2].rule.str[4].param_value2 = model[i].param_value2 + model[i].param_value1 * 0.5f;
			actions[2].rule.str[6].param_value1 = model[i].param_value1 * 0.5f;
			actions[2].rule.str[6].param_value2 = model[i].param_value2 + model[i].param_value1 * 0.5f;
			actions[2].rule.str[8].param_value1 = model[i].param_value1 * LENGTH_ATTENUATION;
			actions[2].rule.str[8].param_value2 = model[i].param_value2 + model[i].param_value1;
			actions[3].rule.str[0].param_value1 = model[i].param_value1 * 0.5f;
			actions[3].rule.str[0].param_value2 = model[i].param_value2;
			actions[3].rule.str[5].param_value1 = model[i].param_value1 * 0.5f;
			actions[3].rule.str[5].param_value2 = model[i].param_value2 + model[i].param_value1 * 0.5f;
			actions[3].rule.str[8].param_value1 = model[i].param_value1 * LENGTH_ATTENUATION;
			actions[3].rule.str[8].param_value2 = model[i].param_value2 + model[i].param_value1;
		} else {
			actions = actions_template["X3"];
			actions[1].rule.str[0].param_value1 = model[i].param_value1 * 0.5f;
			actions[1].rule.str[0].param_value2 = model[i].param_value2;
			actions[1].rule.str[5].param_value1 = model[i].param_value1 * 0.5f;
			actions[1].rule.str[5].param_value2 = model[i].param_value2 + model[i].param_value1 * 0.5f;
			actions[1].rule.str[8].param_value1 = model[i].param_value1 * LENGTH_ATTENUATION;
			actions[1].rule.str[8].param_value2 = model[i].param_value2 + model[i].param_value1;
		}
	} else if (model[i].name == "Y") {
		return actions_template["Y"];
	} else if (model[i].name == "-" || model[i].name == "+") {
		return actions_template[model[i].name];
	} else if (model[i].name == "#") {
		return actions_template["#"];
	} else if (model[i].name == "\\") {
		return actions_template["\\"];
	}

	return actions;
}

/**
 * 現在の座標を計算して返却する。
 */
glm::vec2 ParametricLSystem::computeCurrentPoint(const String& model, const glm::mat4& mvpMat, glm::mat4& modelMat) {
	std::list<glm::mat4> stack;

	int undefined = 0;
	for (int i = 0; i < model.cursor; ++i) {
		if (undefined == 0 && !model[i].param_defined) {
			undefined = 1;
			continue;
		}

		if (model[i].name == "[") {
			stack.push_back(modelMat);
			if (undefined > 0) undefined++;
		} else if (model[i].name == "]") {
			if (!stack.empty()) {
				modelMat = stack.back();
				stack.pop_back();
				if (undefined > 0) undefined--;
			}
		} else if (undefined > 0) {
			continue;
		} else if (model[i].name == "+") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "-") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "#") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 0, 1));
		} else if (model[i].name == "\\") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(0, 1, 0));
		} else if (model[i].name == "/") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(0, 1, 0));
		} else if (model[i].name == "&") {
			modelMat = glm::rotate(modelMat, deg2rad(model[i].param_value1), glm::vec3(1, 0, 0));
		} else if (model[i].name == "^") {
			modelMat = glm::rotate(modelMat, deg2rad(-model[i].param_value1), glm::vec3(1, 0, 0));
		} else if (model[i].name == "f") {
			double length = model[i].param_value1;
			modelMat = glm::translate(modelMat, glm::vec3(0, length, 0));
		} else if (model[i].name == "F") {
			double length = model[i].param_value1;
			modelMat = glm::translate(modelMat, glm::vec3(0, length, 0));
		}
	}

	glm::vec4 p(0, 0, 0, 1);
	p = mvpMat * modelMat * p;

	return glm::vec2((p.x / p.w + 1.0) * GRID_SIZE * 0.5, (p.y / p.w + 1.0) * GRID_SIZE * 0.5);
}

/**
 * 指定されたノードをルートとする探索木のメモリを解放する
 *
 * @param node		ノード
 */
void ParametricLSystem::releaseNodeMemory(Node* node) {
	for (int i = 0; i < node->children.size(); ++i) {
		if (node->children[i] != NULL) {
			releaseNodeMemory(node->children[i]);
		}
	}
	delete node;
	node = NULL;
}

float deg2rad(float deg) {
	return deg * M_PI / 180.0;
}

}
