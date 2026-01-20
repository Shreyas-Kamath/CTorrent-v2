#include "PieceManager.hpp"
#include "FileManager.hpp"

#include <ranges>
#include <algorithm>
#include <iostream>
#include <print>

#include <openssl/sha.h>

PieceManager::PieceManager(boost::asio::any_io_executor disk_exec, size_t num_pieces, size_t piece_length, size_t total_size, const std::vector<std::array<unsigned char, 20>>& piece_hashes, FileManager& fm, std::function<void(uint32_t)> callback): 
        _disk_exec(disk_exec),
        _num_pieces(num_pieces),
        _piece_length(piece_length),
        _total_size(total_size),
        _piece_hashes(piece_hashes),
        _fm(fm),
        _piece_complete_callback(std::move(callback))
    {
        _my_bitfield.resize((_num_pieces + 7) / 8);
        _pieces.resize(_num_pieces);

        auto completed = _fm.read_save_file();
        if (completed) {
            for (auto piece: *completed) {
                downloaded += piece_length_for_index(piece);
                ++_completed_pieces;
                _pieces[piece].is_complete = true;
            }
        }
    }

boost::asio::awaitable<std::optional<std::vector<unsigned char>>> PieceManager::async_fetch_block(uint32_t piece, uint32_t begin, uint32_t length) {

    // launch reads from disk executor
    auto data = co_await boost::asio::co_spawn(
        _disk_exec,
        _fm.read_block(piece, begin, length),
        boost::asio::use_awaitable
    );

    if (data) { uploaded += data->size(); co_return data; }
    co_return std::nullopt;
}

std::vector<uint8_t> PieceManager::fetch_my_bitset() const {
    return _my_bitfield;
}

// lazy init a piece
void PieceManager::lazy_init(uint32_t piece_index) {
    auto& piece = _pieces[piece_index];

    if (piece.block_status.empty()) {
        piece.is_complete = false;
        auto curr_length = piece_length_for_index(piece_index);
        piece.data.resize(curr_length);
        size_t num_blocks = (curr_length + 16383) / 16384;
        piece.block_status.resize(num_blocks, BlockState::NotRequested);
    }
}

bool PieceManager::verify_hash(uint32_t piece_index) {
    unsigned char digest[SHA_DIGEST_LENGTH];
    const auto& data = _pieces[piece_index].data;
    SHA1(data.data(), data.size(), digest);

    return std::equal(std::begin(digest), std::end(digest), _piece_hashes[piece_index].begin());
}

// find the length of a piece, by index
size_t PieceManager::piece_length_for_index(int piece_index) const {
    // assert(piece_index >= 0 && piece_index < _pieces.size() && "Index out of bounds");
    return piece_index < _num_pieces - 1 ? _piece_length : _total_size - _piece_length * (_num_pieces - 1);
}

void PieceManager::add_block(uint32_t piece, uint32_t begin, std::span<const unsigned char> block) {
    auto& curr_piece = _pieces[piece];
    auto block_index = begin / 16384;

    if (curr_piece.is_complete) return;
    if (block_index >= curr_piece.block_status.size()) return;

    auto& curr_block_status = curr_piece.block_status[block_index];
    if (curr_block_status == BlockState::Received) return;

    curr_block_status = BlockState::Received;
    ++curr_piece.blocks_received;

    assert(!curr_piece.data.empty() && "about to copy data into empty piece_data vector");

    std::copy(block.begin(), block.end(), curr_piece.data.begin() + begin);

    if (curr_piece.blocks_received == curr_piece.block_status.size()) {
        if (verify_hash(piece)) {
            curr_piece.is_complete = true;

            // mark as complete in my bitfield
            set_my_bitfield(piece);
            _piece_complete_callback(piece);
            ++_completed_pieces;

            downloaded += curr_piece.data.size();

            // std::cout << "Finished " << _completed_pieces << '/' << _num_pieces << '\n';

            // fire-and-forget to filemanager
            boost::asio::co_spawn(
                _disk_exec,
                _fm.write_piece(piece, std::move(curr_piece.data)),
                boost::asio::detached
            );

            // clear the data immediately to avoid choking up RAM
            curr_piece.data.clear(); curr_piece.data.shrink_to_fit();
            curr_piece.block_status.clear(); curr_piece.block_status.shrink_to_fit();
            curr_piece.blocks_received = 0;
        }
        else {
            // reset block
            curr_piece.data.clear();
            curr_piece.block_status.clear();
            curr_piece.is_complete = false;
            curr_piece.blocks_received = 0;
        }
    }
}

void PieceManager::set_my_bitfield(uint32_t piece) {
    _my_bitfield[piece / 8] |= (1 << (7 - (piece % 8)));
}

// block has timed out
void PieceManager::return_block(uint32_t piece, uint32_t begin) {
    if (piece >= _pieces.size()) return;

    auto& curr_piece = _pieces[piece];
    if (curr_piece.is_complete) return;

    auto block_index = begin / 16384;

    if (block_index >= curr_piece.block_status.size()) return;

    // std::println("Returning piece {}, block {}", piece, block_index);
    if (curr_piece.block_status[block_index] == BlockState::Requested) curr_piece.block_status[block_index] = BlockState::NotRequested;
}

uint64_t PieceManager::downloaded_bytes() const {
    return downloaded;
}

uint64_t PieceManager::uploaded_bytes() const {
    return uploaded;
}

uint64_t PieceManager::total_bytes() const {
    return _total_size;
}

bool PieceManager::is_complete() const {
    return _completed_pieces == _num_pieces;
}

bool PieceManager::is_piece_complete(uint32_t piece) const {
    return _pieces[piece].is_complete;
}

inline bool PieceManager::endgame_required() const {
    auto progress = static_cast<double>(_completed_pieces) / _num_pieces * 100.0;
    return progress >= 90;
}

// return piece_index, offset, length, or nullopt, if nothing
// std::optional<std::tuple<int, int, int>> PieceManager::next_block_request(const boost::dynamic_bitset<>& peer_bitfield) {
//     if (_completed_pieces == _num_pieces) {
//         endgame = false;
//         return std::nullopt;
//     }
    
//     assert(peer_bitfield.size() == _num_pieces && "Bitfield size mismatch");

//     for (int i{}; i < _num_pieces; ++i) {
//         if (_pieces[i].is_complete) continue;

//         if (peer_bitfield.test(i)) {
//             lazy_init((uint32_t)i);
//             auto& curr = _pieces[i];
//             for (int j{}; j < curr.block_status.size(); ++j) {
//                 auto& status = curr.block_status[j];
//                 if (status == BlockState::NotRequested) {
//                     status = BlockState::Requested;
//                     return std::make_tuple(i, j * 16384, std::min(16384, (int)piece_length_for_index(i) - j * 16384));
//                 }
//             }
//         }
//     }

//     return std::nullopt;
// }

// Experimental requests with endgame

std::optional<std::tuple<int, int, int>> PieceManager::next_block_request(const boost::dynamic_bitset<>& peer_bitfield)
{
    if (_completed_pieces == _num_pieces) {
        endgame = false;
        return std::nullopt;
    }

    assert(peer_bitfield.size() == _num_pieces && "Bitfield size mismatch");

    endgame = endgame_required();

    const uint32_t piece_start = endgame ? endgame_cursor % _num_pieces : 0;

    for (uint32_t di = 0; di < _num_pieces; ++di) {
        uint32_t i = (piece_start + di) % _num_pieces;

        if (_pieces[i].is_complete || !peer_bitfield.test(i)) continue;

        lazy_init(i);
        auto& piece = _pieces[i];

        for (uint32_t j = 0; j < piece.block_status.size(); ++j) {
            auto& status = piece.block_status[j];

            // ---- NORMAL MODE ----
            if (!endgame) {
                if (status != BlockState::NotRequested) continue;
                status = BlockState::Requested;
                return std::make_tuple(i, j * 16384, std::min(16384, (int)piece_length_for_index(i) - (int)(j * 16384)));
            }

            // ---- ENDGAME MODE ----
            if (endgame && status != BlockState::Received) {
                // only mark if this is the first request
                if (status == BlockState::NotRequested) status = BlockState::Requested;
                endgame_cursor = i + 1;
                return std::make_tuple(i, j * 16384, std::min(16384, (int)piece_length_for_index(i) - (int)(j * 16384)));
            }
        }
    }

    return std::nullopt;
}

// bool PieceManager::should_be_interested(std::optional<uint32_t> piece = std::nullopt) {
//     if (piece) {
//         if (!)
//     }
// } 