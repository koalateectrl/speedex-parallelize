
#include "rpc/consensus_api.h"

#include <xdrpp/marshal.h>

#include "simple_debug.h"

namespace edce {


void
ValidatorCaller::validate_block(const HashedBlock& new_header, std::unique_ptr<SerializedBlock>&& new_block) {

  std::lock_guard lock(mtx);

  blocks.emplace_back(new_header, std::move(new_block));
//  block = std::move(new_block);
  //header = new_header;
  cv.notify_all();
}

void
ValidatorCaller::run() {
  BLOCK_INFO("starting validator caller async task thread");

  while(true) {
    std::unique_lock lock(mtx);
    if ((!done_flag) && (!exists_work_to_do())) {
      cv.wait(lock, [this] () {return done_flag || exists_work_to_do();});
    }

    if (done_flag) return;

    if (blocks.size() > 0) {

      auto [header, block] = std::move(blocks[0]);
      blocks.erase(blocks.begin());
      in_progress = true;
      lock.unlock();

      auto res = main_node.validate_block(header, std::move(block));
      if (!res) {
        BLOCK_INFO("block validation failed!!!");
      } else {
        BLOCK_INFO("block validation succeeded");
      }

      //block = nullptr;
      lock.lock();
      in_progress = false;
    }
    cv.notify_all();
  }
}

void
AcknowledgeCaller::ack_block(uint64_t new_block) {
  std::lock_guard lock(mtx);
  if (!block_to_confirm) {
    block_to_confirm = new_block;
  } else {
    if (*block_to_confirm < new_block) {
      block_to_confirm = new_block;
    }
  }
  cv.notify_all();
}

void
AcknowledgeCaller::run() {
  BLOCK_INFO("starting ack caller async thread");
  while(true) {
    std::unique_lock lock(mtx);
    if ((!done_flag) && (!exists_work_to_do())) {
      cv.wait(lock, [this] () {return done_flag || exists_work_to_do();});
    }
    if (done_flag) return;
    if (block_to_confirm) {
      
      uint64_t copied_block = *block_to_confirm;
      in_progress = true;
      block_to_confirm = std::nullopt;
      lock.unlock();

      main_node.log_block_confirmation(copied_block);

      lock.lock();
      in_progress = false;
    }
    cv.notify_all();
  }
}


void
BlockTransferV1_server::send_block(const HashedBlock &header, std::unique_ptr<SerializedBlock> block)
{
  BLOCK_INFO("got new block for header number %lu", header.block.blockNumber);

  caller.validate_block(header, std::move(block));
}

void
BlockAcknowledgeV1_server::ack_block(const uint64 &block_number)
{
  BLOCK_INFO("received acknowledgment of block %lu", block_number);
  caller.ack_block(block_number);
  //main_node.log_block_confirmation(block_number);
}

void
RequestBlockForwardingV1_server::request_forwarding(const hostname &arg)
{
  BLOCK_INFO("adding forwarding target to my targets %s", arg.c_str());
  main_node.get_connection_manager().add_forwarding_target(arg); 
  BLOCK_INFO("done adding forwarding target");
}

void
ExperimentControlV1_server::write_measurements()
{
	BLOCK_INFO("forcing measurements to be logged to disk");
	main_node.write_measurements(); 
}

void
ExperimentControlV1_server::signal_start()
{
  BLOCK_INFO("got signal to start experiment");
  std::unique_lock lock(wait_mtx);

  wakeable = true;
  wait_cv.notify_all();
}

void 
ExperimentControlV1_server::signal_upstream_finish() {
  BLOCK_INFO("got signal that upstream is done");
  std::unique_lock lock(wait_mtx);
  upstream_finished = true;
  wait_cv.notify_all();
}


std::unique_ptr<ExperimentResultsUnion>
ExperimentControlV1_server::get_measurements() {
  return std::make_unique<ExperimentResultsUnion>(main_node.get_measurements());
}

std::unique_ptr<unsigned int>
ExperimentControlV1_server::is_running() {
  return std::make_unique<unsigned int>(experiment_finished ? 0 : 1);
}

std::unique_ptr<unsigned int>
ExperimentControlV1_server::is_ready_to_start() {
  return std::make_unique<unsigned int>(experiment_ready_to_start ? 1 : 0);
}

void
ExperimentControlV1_server::wait_for_start() {
  BLOCK_INFO("waiting for experiment control signal");
  std::unique_lock lock(wait_mtx);
  if (wakeable) {
    wakeable = false;
    return;
  }
  wait_cv.wait(lock, [this] { return wakeable;});
  BLOCK_INFO("woke up from experiment control signal");
  wakeable = false;
}

void
ExperimentControlV1_server::wait_for_upstream_finish() {
  BLOCK_INFO("waiting for upstream finished signal");
  std::unique_lock lock(wait_mtx);
  if (upstream_finished) {
    return;
  }
  wait_cv.wait(lock, [this] { return upstream_finished.load();});
}


}
