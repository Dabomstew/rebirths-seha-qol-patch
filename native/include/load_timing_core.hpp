#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <cstddef>

namespace rebirths {
// Closed, confirmed intervals only: callers never extrapolate a missing poll.
// One serialized writer; owner tokens are never dereferenced by this reducer.
class LoadTimingCore {
    struct Anchor { uint32_t owner = 0, phase = 0; uint64_t qpc = 0; bool pending = false; uint64_t began = 0; };
    std::array<Anchor, 3> anchors_{};
    struct IntervalRange { uint64_t begin = 0, end = 0; };
    std::array<IntervalRange, 16> intervals_{};
    size_t intervalCount_ = 0;
    uint64_t sealedEnd_ = 0;
    uint64_t maximumGap_ = 0, clock_ = 0, completed_ = 0;
    uint32_t transitions_ = 0;
    bool fault_ = false;
    bool Clock(uint64_t qpc) noexcept {
        if (fault_ || !qpc || qpc < clock_) { fault_ = true; anchors_ = {}; return false; }
        clock_ = qpc; return true;
    }
    void Trim() noexcept {
        uint64_t earliest = clock_;
        for (const auto& anchor : anchors_) if (anchor.owner &&
            (anchor.pending || !maximumGap_ || clock_ - anchor.qpc <= maximumGap_)) earliest = std::min(earliest, anchor.qpc);
        while (intervalCount_ && intervals_[0].end <= earliest) {
            sealedEnd_ = intervals_[0].end;
            for (size_t i = 1; i < intervalCount_; ++i) intervals_[i - 1] = intervals_[i];
            --intervalCount_;
        }
    }
    void Interval(uint64_t begin, uint64_t end) noexcept {
        begin = std::max(begin, sealedEnd_);
        if (end <= begin) return;
        uint64_t replaced = 0;
        size_t first = 0;
        while (first < intervalCount_ && intervals_[first].end < begin) ++first;
        size_t last = first;
        while (last < intervalCount_ && intervals_[last].begin <= end) {
            begin = std::min(begin, intervals_[last].begin);
            end = std::max(end, intervals_[last].end);
            replaced += intervals_[last].end - intervals_[last].begin;
            ++last;
        }
        if (first == last && intervalCount_ == intervals_.size()) { fault_ = true; anchors_ = {}; return; }
        if (last == first) {
            for (size_t i = intervalCount_; i > first; --i) intervals_[i] = intervals_[i - 1];
            ++intervalCount_;
        } else {
            for (size_t i = last; i < intervalCount_; ++i) intervals_[first + 1 + i - last] = intervals_[i];
            intervalCount_ -= last - first - 1;
        }
        intervals_[first] = {begin, end}; completed_ += end - begin - replaced;
    }
public:
    explicit LoadTimingCore(uint64_t maximumGap = 0) noexcept : maximumGap_(maximumGap) {}
    void Reset(uint64_t maximumGap) noexcept { *this = LoadTimingCore(maximumGap); }
    uint64_t Completed() const noexcept { return completed_; }
    uint64_t LastQpc() const noexcept { return clock_; }
    uint64_t Frontier() const noexcept {
        uint64_t frontier=clock_;
        for(const auto& anchor:anchors_)if(anchor.owner&&(anchor.pending||!maximumGap_||clock_-anchor.qpc<=maximumGap_))
            frontier=std::min(frontier,anchor.qpc);
        return frontier;
    }
    uint64_t Settled() const noexcept {
        uint64_t unconfirmed=0;const auto frontier=Frontier();
        for(size_t i=0;i<intervalCount_;++i)if(intervals_[i].end>frontier)
            unconfirmed+=intervals_[i].end-std::max(frontier,intervals_[i].begin);
        return completed_-unconfirmed;
    }
    uint32_t Transitions() const noexcept { return transitions_; }
    bool Faulted() const noexcept { return fault_; }
    void Fail() noexcept { fault_=true;anchors_={}; }
    void Cancel(unsigned kind) noexcept {
        if (kind < anchors_.size() && anchors_[kind].owner) { anchors_[kind] = {}; ++transitions_; Trim(); }
    }
    void Begin(unsigned kind, uint32_t owner, uint32_t phase, bool eligible, bool focused, uint64_t qpc) noexcept {
        if (kind >= anchors_.size() || !Clock(qpc)) return;
        const auto old = anchors_[kind]; anchors_[kind] = {};
        if (old.owner) ++transitions_;
        if(old.pending){Fail();return;}
        if (eligible && focused && owner && old.owner == owner && old.phase == phase &&
            qpc >= old.qpc && (!maximumGap_ || qpc - old.qpc <= maximumGap_))
            anchors_[kind]={owner,phase,old.qpc,true,qpc};
        else if(eligible&&focused&&owner)anchors_[kind]={owner,phase,qpc,true,qpc};
        Trim();
    }
    void End(unsigned kind, uint32_t owner, uint32_t phase, bool blockedResource,
             bool focused, uint64_t began, uint64_t qpc) noexcept {
        if (kind >= anchors_.size() || !Clock(qpc)) return;
        const auto pending=anchors_[kind];anchors_[kind]={};
        if (began > qpc) { fault_ = true; anchors_ = {}; return; }
        if (!blockedResource || !focused || !owner || (maximumGap_ && qpc - began > maximumGap_)) { Trim(); return; }
        if(!pending.pending || pending.began!=began){Fail();return;}
        if(pending.owner!=owner || pending.phase!=phase){Trim();return;}
        // A prior blocked gap is admitted only after this poll confirms waiting.
        Interval(pending.qpc, qpc);
        if (fault_) return;
        anchors_[kind] = {owner, phase, qpc}; ++transitions_; Trim();
    }
};
}
