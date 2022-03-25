#include "search.hpp"
#include <chess/position.hpp>
#include <limits>
#include <tt.hpp>
#include "../eval/eval.hpp"
#include "../ttentry.hpp"
#include "qsearch.hpp"
#include "sort.hpp"

namespace swizzles::search {

[[nodiscard]] auto search(ThreadData &td,
                          SearchStack *ss,
                          chess::Position &pos,
                          int alpha,
                          int beta,
                          int depth) noexcept -> int {
    td.seldepth = std::max(td.seldepth, ss->ply);
    const auto alpha_orig = alpha;

    const auto ttentry = td.tt->poll(pos.hash());
    if (ttentry.hash == pos.hash() && ttentry.depth >= depth) {
        const auto eval = eval_from_tt(ttentry.eval, ss->ply);

        if (ttentry.flag == TTFlag::Exact) {
            ss->pv.clear();
            ss->pv.push_back(ttentry.move);
            return eval;
        } else if (ttentry.flag == TTFlag::Lower) {
            alpha = std::max(alpha, eval);
        } else if (ttentry.flag == TTFlag::Upper) {
            beta = std::min(beta, eval);
        }

        if (alpha >= beta) {
            ss->pv.clear();
            ss->pv.push_back(ttentry.move);
            return eval;
        }
    }

    const auto is_root = ss->ply == 0;
    const auto in_check = pos.is_attacked(pos.get_kings(pos.turn()), !pos.turn());

    if (in_check) {
        depth++;
    }

    if (depth == 0 || ss->ply == max_depth) {
        return qsearch(pos, alpha, beta);
    }

    if (pos.halfmoves() >= 100) {
        return 0;
    }

    if (const auto repeats = pos.num_repeats(); (is_root && repeats == 3) || (!is_root && repeats == 2)) {
        return 0;
    }

    td.controller->update();

    if (td.controller->should_stop()) {
        return 0;
    }

    auto best_score = std::numeric_limits<int>::min();
    auto best_move = chess::Move();
    auto moves = pos.movegen();

    sort(moves, ttentry.move);

    for (const auto &move : moves) {
        pos.makemove(move);

        if (pos.is_attacked(pos.get_king(!pos.turn()), pos.turn())) {
            pos.undomove();
            continue;
        }

        td.nodes++;

        const auto score = -search(td, ss + 1, pos, -beta, -alpha, depth - 1);
        pos.undomove();

        if (score > best_score) {
            best_score = score;
            best_move = move;
            ss->pv.clear();
            ss->pv.push_back(move);
        }

        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }

    if (best_score == std::numeric_limits<int>::min()) {
        if (in_check) {
            return -mate_score + ss->ply;
        } else {
            return 0;
        }
    }

    auto new_ttentry = TTEntry();
    new_ttentry.eval = eval_to_tt(best_score, ss->ply);
    if (best_score <= alpha_orig) {
        new_ttentry.flag = TTFlag::Upper;
    } else if (best_score >= beta) {
        new_ttentry.flag = TTFlag::Lower;
    } else {
        new_ttentry.flag = TTFlag::Exact;
    }
    new_ttentry.depth = depth;
    new_ttentry.move = best_move;
    td.tt->add(pos.hash(), new_ttentry);

    return best_score;
}

}  // namespace swizzles::search
