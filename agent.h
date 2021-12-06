/**
 * Framework for NoGo and similar games (C++ 11)
 * agent.h: Define the behavior of variants of the player
 *
 * Author: Theory of Computer Games (TCG 2021)
 *         Computer Games and Intelligence (CGI) Lab, NYCU, Taiwan
 *         https://cgilab.nctu.edu.tw/
 */

#pragma once
#include <string>
#include <random>
#include <sstream>
#include <map>
#include <type_traits>
#include <algorithm>
#include "board.h"
#include "action.h"
#include <fstream>
#include <cstdlib>
#include <ctime>

class agent {
public:
	agent(const std::string& args = "") {
		std::stringstream ss("name=unknown role=unknown " + args);
		for (std::string pair; ss >> pair; ) {
			std::string key = pair.substr(0, pair.find('='));
			std::string value = pair.substr(pair.find('=') + 1);
			meta[key] = { value };
		}
	}
	virtual ~agent() {}
	virtual void open_episode(const std::string& flag = "") {}
	virtual void close_episode(const std::string& flag = "") {}
	virtual action take_action(const board& b) { return action(); }
	virtual bool check_for_win(const board& b) { return false; }

public:
	virtual std::string property(const std::string& key) const { return meta.at(key); }
	virtual void notify(const std::string& msg) { meta[msg.substr(0, msg.find('='))] = { msg.substr(msg.find('=') + 1) }; }
	virtual std::string name() const { return property("name"); }
	virtual std::string role() const { return property("role"); }

protected:
	typedef std::string key;
	struct value {
		std::string value;
		operator std::string() const { return value; }
		template<typename numeric, typename = typename std::enable_if<std::is_arithmetic<numeric>::value, numeric>::type>
		operator numeric() const { return numeric(std::stod(value)); }
	};
	std::map<key, value> meta;
};

/**
 * base agent for agents with randomness
 */
class random_agent : public agent {
public:
	random_agent(const std::string& args = "") : agent(args) {
		if (meta.find("seed") != meta.end())
			engine.seed(int(meta["seed"]));
		if (meta.find("c") != meta.end())
			c = float(meta["c"]);
		if (meta.find("random") != meta.end())
			random_player = true;
		if (meta.find("n") != meta.end())
			simulation_count = int(meta["n"]);
	}
	virtual ~random_agent() {}

protected:
	std::default_random_engine engine;
	float c;
	bool random_player = false;
	int simulation_count;
};

/**
 * random player for both side
 * put a legal piece randomly
 */
class player : public random_agent {
public:
	player(const std::string& args = "") : random_agent("name=random role=unknown " + args),
		space(board::size_x * board::size_y), who(board::empty) {
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++)
			space[i] = action::place(i, who);
	}

	virtual action take_action(const board& state) {
		std::shuffle(space.begin(), space.end(), engine);
		for (const action::place& move : space) {
			board after = state;
			if (move.apply(after) == board::legal)
				return move;
		}
		return action();
	}

private:
	std::vector<action::place> space;
	board::piece_type who;
};

struct node{
	int total = 0;
	int win = 0;
	node* parent = NULL;
	std::vector<node*> children;
	board state;
	action move;
};

class MCTS_player : public random_agent {
public:
	MCTS_player(const std::string& args = "") : random_agent("name=random role=unknown " + args),
		space(board::size_x * board::size_y), who(board::empty) ,root(NULL){
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++)
			space[i] = action::place(i, who);

		who_cpy = who;
	}

	float UCT(node* cur){
		float win_rate = (float) cur->win / (float) cur->total;
		float exploitation = sqrt(log(cur->parent->total) / (float) cur->total);
		float uct = win_rate + c * exploitation;

		return uct; 
	}

	void change_player() {
		if(who == board::black) who = board::white;
		else if(who == board::white) who = board::black;
		
		return;
	}

	node* select(){
		node* cur = root;
		who = who_cpy;

		while(cur->children.size() != 0){
			std::shuffle(cur->children.begin(), cur->children.end(), engine);
			float best_uct = -1;
			node* best_child = NULL;

			for(node* child : cur->children){
				if(child->total == 0){
					best_child = child;
					break;
				}

				float uct = UCT(child);
				if(uct > best_uct){
					uct = best_uct;
					best_child = child;
				}
			}

			cur = best_child;
			change_player();
		}

		return cur;
	}
	
	void expand(node* leaf) {
		for (const action::place& move : space) {
			board after = leaf->state;
			if (move.apply(after, who) == board::legal){
				node* child = new node;
				child->state = after;
				leaf->children.push_back(child);
				child->parent = leaf;
				child->move = move;
			}
		}

		return;
	}

	node* random_child(node* leaf){
		std::shuffle(leaf->children.begin(), leaf->children.end(), engine);
		node* child = leaf->children[0];		
		return child;
	}

	bool is_terminal(const board& cur_state) {
		int legal_count = 0;
		
		for (const action::place& move : space) {
			board after = cur_state;
			if (move.apply(after) == board::legal) legal_count++;
		}

		if(legal_count == 0) { //我沒有 legal action 了
			if(who == board::black) winner = board::white;
			else if(who == board::white) winner = board::black;
			return true;
		}

		change_player();
		legal_count = 0;

		for (const action::place& move : space) {
			board after = cur_state;
			if (move.apply(after) == board::legal) legal_count++;
		}

		change_player();
		if(legal_count == 0) { // 對方沒有 legal action 了
			if(who == board::black) winner = board::black;
			else if(who == board::white) winner = board::white;
			return true;
		}
		
		return false;
	}

	int simulation(node* child) {
		board cur_state = child->state;

		while(!is_terminal(cur_state)){
			change_player();

			std::shuffle(space.begin(), space.end(), engine);
			for (const action::place& move : space) {
				board after = cur_state;
				if (move.apply(after) == board::legal){
					cur_state = after;
					break;
				}
			}
		}
		
		who = who_cpy;
		if(winner == who) return 1;
		else return 0;
	}

	void backpropagate(node* child, int result){
		node* cur = child;

		while(cur != root){
			cur->total++;
			cur->win += result;
			cur = cur->parent;
		}

		root->total++;
		root->win += result;

		return;
	}

	void mcts(){
		for(int i = 1 ; i <= simulation_count ; i++){
			node* leaf = select();
			expand(leaf);

			if(leaf->children.empty()){
				int result = simulation(leaf);
				backpropagate(leaf, result);
				continue;
			}

			node* child = random_child(leaf);
			int result = simulation(child);
			backpropagate(child, result);
		}

		return;
	}

	void delete_tree(){
		node* cur = root;
		while(root->children.size() != 0){
			while(cur->children.size() != 0) cur = cur->children.back();
			node* tmp = cur->parent;
			tmp->children.pop_back();
			delete(cur);
			cur = tmp;
		}

		delete(root);
		return;
	}

	virtual action take_action(const board& state) {
		if(random_player){
			std::shuffle(space.begin(), space.end(), engine);
			for (const action::place& move : space) {
				board after = state;
				if (move.apply(after) == board::legal)
					return move;
			}
			return action();
		}

		root = new node;
		root->state = state;
		mcts();

		action best_move = action();
		float best_uct = -1;
		for(node* child : root->children){
			float uct = UCT(child);

			if(uct > best_uct){
				best_uct = uct;
				best_move = child->move;
			}
		}

		delete_tree();
		return best_move;
	}

private:
	std::vector<action::place> space;
	board::piece_type who;
	board::piece_type who_cpy;
	board::piece_type winner;
	node* root;
};