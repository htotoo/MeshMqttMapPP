#ifndef MESSAGEIDTRACKER_HPP
#define MESSAGEIDTRACKER_HPP

class MessageIdTracker {
   public:
    bool check(uint32_t msgid) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (msgids_.find(msgid) != msgids_.end()) {
            return true;
        }
        if (msgids_.size() >= 50) {
            // Remove the oldest inserted msgid
            msgids_order_.pop_front();
            msgids_.erase(msgids_order_.front());
        }
        msgids_order_.push_back(msgid);
        msgids_.insert(msgid);
        return false;
    }

   private:
    std::unordered_set<uint32_t> msgids_;
    std::deque<uint32_t> msgids_order_;
    std::mutex mutex_;
};

#endif  // MESSAGEIDTRACKER_HPP