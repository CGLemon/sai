/*
    This file is part of Leela Zero.
    Copyright (C) 2017-2018 Gian-Carlo Pascutto and contributors
    Copyright (C) 2018 SAI Team

    Leela Zero is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Leela Zero is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Leela Zero.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef UCTNODE_H_INCLUDED
#define UCTNODE_H_INCLUDED

#include "config.h"

#include <atomic>
#include <memory>
#include <vector>
#include <cassert>
#include <cstring>

#include "GameState.h"
#include "Network.h"
#include "SMP.h"
#include "UCTNodePointer.h"
#include "UCTSearch.h"

class UCTNode {
public:
    // When we visit a node, add this amount of virtual losses
    // to it to encourage other CPUs to explore other parts of the
    // search tree.
    static constexpr auto VIRTUAL_LOSS_COUNT = 3;
    // Defined in UCTNode.cpp
    explicit UCTNode(int vertex, float policy);
    UCTNode() = delete;
    ~UCTNode() = default;

    bool create_children(Network & network,
                         std::atomic<int>& nodecount,
                         GameState& state, float& value, float& alpkt,
			             float& beta,
                         float min_psa_ratio = 0.0f);

    const std::vector<UCTNodePointer>& get_children() const;
    void sort_children(int color);
    void sort_children_by_policy();
    UCTNode& get_best_root_child(int color);
    UCTNode* uct_select_child(const GameState & currstate, bool is_root,
                              int max_visits,
                              std::vector<int> move_list,
                              bool nopass = false);

    size_t count_nodes_and_clear_expand_state();
    bool first_visit() const;
    bool has_children() const;
    bool expandable(const float min_psa_ratio = 0.0f) const;
    void invalidate();
    void set_active(const bool active);
    bool valid() const;
    bool active() const;
    double get_blackevals() const;
    int get_move() const;
    int get_visits() const;
    float get_policy() const;
    void set_policy(float policy);
    float get_eval(int tomove) const;
    float get_raw_eval(int tomove, int virtual_loss = 0) const;
    float get_net_eval(int tomove) const;
    float get_agent_eval(int tomove) const;
    float get_eval_bonus() const;
    float get_eval_bonus_father() const;
    void set_eval_bonus_father(float bonus);
    float get_eval_base() const;
    float get_eval_base_father() const;
    void set_eval_base_father(float bonus);
    float get_net_eval() const;
    float get_net_beta() const;
    float get_net_alpkt() const;
    void set_values(float value, float alpkt, float beta);
    void set_progid(int id);
    int get_progid() const;
#ifndef NDEBUG
    void set_urgency(float urgency, float psa, float q,
                     float num, float den);
    std::array<float, 5> get_urgency() const;
#endif
    void virtual_loss();
    void virtual_loss_undo();
    void clear_visits();
    void clear_children_visits();
    void update(float eval);

    // Defined in UCTNodeRoot.cpp, only to be called on m_root in UCTSearch
    bool randomize_first_proportionally();
    void prepare_root_node(Network & network, int color,
                           std::atomic<int>& nodecount,
                           GameState& state,
                           bool fast_roll_out = false);

    UCTNode* get_first_child() const;
    UCTNode* get_second_child() const;
    UCTNode* get_nopass_child(FastState& state) const;
    std::unique_ptr<UCTNode> find_child(const int move);
    void inflate_all_children();
    UCTNode* select_child(int move);
    float estimate_alpkt(int passes, bool is_tromptaylor_scoring = false) const;

    void clear_expand_state();
private:
    enum Status : char {
        INVALID, // superko
        PRUNED,
        ACTIVE
    };
    void link_nodelist(std::atomic<int>& nodecount,
                       std::vector<Network::PolicyVertexPair>& nodelist,
                       float min_psa_ratio);
    void accumulate_eval(float eval);
    void kill_superkos(const KoState& state);
    void dirichlet_noise(float epsilon, float alpha);
    void get_subtree_alpkts(std::vector<float> & vector, int passes,
                            bool is_tromptaylor_scoring) const;

    // Note : This class is very size-sensitive as we are going to create
    // tens of millions of instances of these.  Please put extra caution
    // if you want to add/remove/reorder any variables here.

    // Move
    std::int16_t m_move;
    // UCT
    std::atomic<std::int16_t> m_virtual_loss{0};
    std::atomic<int> m_visits{0};
    // UCT eval
    float m_policy;
    // Original net eval for this node (not children).
    float m_net_eval{0.5f};
    //    float m_net_value{0.5f};
    float m_net_alpkt{0.0f}; // alpha + \tilde k
    float m_net_beta{1.0f};
    float m_eval_bonus{0.0f}; // x bar
    float m_eval_base{0.0f}; // x base
    float m_eval_base_father{0.0f}; // x base of father node
    float m_eval_bonus_father{0.0f}; // x bar of father node
    int m_progid{-1}; // progressive unique identifier
#ifndef NDEBUG
    std::array<float, 5> m_last_urgency;
#endif

    // the following is used only in fpu, with reduction
    float m_agent_eval{0.5f}; // eval_with_bonus(eval_bonus()) no father
    std::atomic<double> m_blackevals{0.0};
    std::atomic<Status> m_status{ACTIVE};

    // m_expand_state acts as the lock for m_children.
    // see manipulation methods below for possible state transition
    enum class ExpandState : std::uint8_t {
        // initial state, no children
        INITIAL = 0,

        // creating children.  the thread that changed the node's state to
        // EXPANDING is responsible of finishing the expansion and then
        // move to EXPANDED, or revert to INITIAL if impossible
        EXPANDING,

        // expansion done.  m_children cannot be modified on a multi-thread
        // context, until node is destroyed.
        EXPANDED,
    };
    std::atomic<ExpandState> m_expand_state{ExpandState::INITIAL};

    // Tree data
    std::atomic<float> m_min_psa_ratio_children{2.0f};
    std::vector<UCTNodePointer> m_children;

    //  m_expand_state manipulation methods
    // INITIAL -> EXPANDING
    // Return false if current state is not INITIAL
    bool acquire_expanding();

    // EXPANDING -> DONE
    void expand_done();

    // EXPANDING -> INITIAL
    void expand_cancel();

    // wait until we are on EXPANDED state
    void wait_expanded();
};

#endif
