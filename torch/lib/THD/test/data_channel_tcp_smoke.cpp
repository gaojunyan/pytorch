/*
 * This test is prone to race conditions on acquiring socket and port for listening.
 * To avoid this problem each worker waits some predefined time to let others
 * do their initial work. It is very unlikely such situation will ever occur
 * but this design does **NOT** prevent race conditions.
 *
 * Race conditions on ENV variables have been eliminated by using mutex and
 * reading all ENV variables in DataChannelTCP constructor instead of `init`
 * function where all blocking accept/connect logic is defined.
 */


#include "../base/channels/DataChannelTCP.hpp"
#include "../base/tensors/THTensor.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

constexpr int WORKERS_NUM = 2;
constexpr int MASTER_PORT = 45678;

std::vector<std::thread> g_all_workers;
std::mutex g_mutex;

template<class T>
typename std::enable_if<!std::numeric_limits<T>::is_integer, bool>::type almost_equal(T x, T y, int ulp = 5) {
  return std::abs(x-y) < std::numeric_limits<T>::epsilon() * std::abs(x+y) * ulp
      || std::abs(x-y) < std::numeric_limits<T>::min();
}

void master()
{
  g_mutex.lock();
  setenv("WORLD_SIZE", std::to_string((WORKERS_NUM + 1)).data(), 1);
  setenv("RANK", "0", 1);
  setenv("MASTER_PORT", std::to_string(MASTER_PORT).data(), 1);
  auto masterChannel = std::make_shared<thd::DataChannelTCP>(); // reads all env variable
  g_mutex.unlock();

  assert(masterChannel->init());
  assert(masterChannel->getRank() == 0);
  assert(masterChannel->getNumProcesses() == WORKERS_NUM + 1);

  FloatTensor *float_tensor = new THTensor<float>();
  float_tensor->resize({1, 2, 3});
  float_tensor->fill(4.3);

  // we cannot send to ourselves
  try {
    masterChannel->send(*float_tensor, 0);
    assert(false);
  } catch (const std::logic_error& e) {}

  // send good tensor
  masterChannel->send(*float_tensor, 1);

  // send tensor with different sizes which does not match worker tensor sizes
  float_tensor->resize({1, 2, 3, 4});
  masterChannel->send(*float_tensor, 1);

  // broadcast int tensor
  IntTensor *int_tensor = new THTensor<int>();
  int_tensor->resize({1, 2, 3, 4, 5});
  int_tensor->fill(1000000000);
  masterChannel->broadcast(*int_tensor, 0);

  // test spam broadcast
  for (int i = 0; i < masterChannel->getNumProcesses(); ++i) {
    masterChannel->broadcast(*int_tensor, i);
  }

  // reduce
  float_tensor->resize({1, 2, 3, 4});
  float_tensor->fill(4.3);
  masterChannel->reduce(*float_tensor, THDReduceOp::THDReduceSUM, 0);
  for (int i = 0; i < float_tensor->numel(); i++) {
    assert(almost_equal(
      reinterpret_cast<float*>(float_tensor->data())[i],
      static_cast<float>(4.3 + 2.2 * WORKERS_NUM)
    ));
  }

  // test spam reduce
  for (int i = 0; i < masterChannel->getNumProcesses(); ++i) {
    masterChannel->reduce(*float_tensor, THDReduceOp::THDReduceSUM, i);
  }

  // allReduce
  int_tensor->resize({1, 2, 3, 4, 5});
  int_tensor->fill(1000);
  masterChannel->allReduce(*int_tensor, THDReduceOp::THDReduceSUM);
  for (int i = 0; i < int_tensor->numel(); i++) {
    assert(reinterpret_cast<int*>(int_tensor->data())[i] == (1000 + 10 * WORKERS_NUM));
  }

  // groups
  THDGroup group = masterChannel->newGroup({1, 2});
  int_tensor->resize({1, 2, 3, 4, 5});
  int_tensor->fill(1000);

  /*
   * We call this functions to check if our data does not change and if it will not
   * affect any computations when process outside group join any of this functions.
   *
   * Processes which do not belong to group do not have to call those methods!
   */
  masterChannel->allReduce(*int_tensor, THDReduceOp::THDReduceSUM, group);
  auto tensor_data_ptr = reinterpret_cast<int*>(int_tensor->data());
  for (int i = 0; i < int_tensor->numel(); i++)
    assert(tensor_data_ptr[i] == 1000);

  masterChannel->reduce(*int_tensor, THDReduceOp::THDReduceSUM, 1, group);
  for (int i = 0; i < int_tensor->numel(); i++)
    assert(tensor_data_ptr[i] == 1000);

  masterChannel->broadcast(*int_tensor, 1, group);
  for (int i = 0; i < int_tensor->numel(); i++)
    assert(tensor_data_ptr[i] == 1000);

  // wait for all workers to finish
  for (auto& worker : g_all_workers) {
    worker.join();
  }

  delete float_tensor;
  delete int_tensor;
}

void worker(int id)
{
  g_mutex.lock();
  setenv("RANK", std::to_string(id).data(), 1);
  setenv("MASTER_ADDR", std::string("127.0.0.1:" + std::to_string(MASTER_PORT)).data(), 1);
  auto workerChannel = std::make_shared<thd::DataChannelTCP>(); // reads all env variable

  /*
   * Wait for other processes to initialize.
   * It is to avoid race in acquiring socket and port for listening (in init function).
   */
  std::this_thread::sleep_for(std::chrono::milliseconds(200 * workerChannel->getRank()));
  g_mutex.unlock();

  assert(workerChannel->init());
  assert(workerChannel->getRank() == id);
  assert(workerChannel->getNumProcesses() == WORKERS_NUM + 1);

  FloatTensor *float_tensor = new THTensor<float>();
  float_tensor->resize({1, 2, 3});

  if (workerChannel->getRank() == 1) {
    // receive good tensor
    workerChannel->receive(*float_tensor, 0);

    for (int i = 0; i < float_tensor->numel(); i++) {
      assert(almost_equal(
        reinterpret_cast<float*>(float_tensor->data())[i],
        static_cast<float>(4.3)
      ));
    }

    // new sizes does not match
    try {
      workerChannel->receive(*float_tensor, 0);
      assert(false);
    } catch (const std::logic_error& e) {}
  }

  // get broadcasted tensor
  IntTensor *int_tensor = new THTensor<int>();
  int_tensor->resize({1, 2, 3, 4, 5});
  workerChannel->broadcast(*int_tensor, 0);
  for (int i = 0; i < int_tensor->numel(); i++) {
    assert(reinterpret_cast<int*>(int_tensor->data())[i] == 1000000000);
  }

  // test spam broadcast
  for (int i = 0; i < workerChannel->getNumProcesses(); ++i) {
    workerChannel->broadcast(*int_tensor, i);
  }

  // reduce
  float_tensor->resize({1, 2, 3, 4});
  float_tensor->fill(2.2);
  workerChannel->reduce(*float_tensor, THDReduceOp::THDReduceSUM, 0);
  for (int i = 0; i < float_tensor->numel(); i++) { // tensor values should not change
    assert(almost_equal(
      reinterpret_cast<float*>(float_tensor->data())[i],
      static_cast<float>(2.2)
    ));
  }

  // test spam reduce
  for (int i = 0; i < workerChannel->getNumProcesses(); ++i) {
    workerChannel->reduce(*float_tensor, THDReduceOp::THDReduceSUM, i);
  }

  // allReduce
  int_tensor->resize({1, 2, 3, 4, 5});
  int_tensor->fill(10);
  workerChannel->allReduce(*int_tensor, THDReduceOp::THDReduceSUM);
  for (int i = 0; i < int_tensor->numel(); i++)
    assert(reinterpret_cast<int*>(int_tensor->data())[i] == (1000 + 10 * WORKERS_NUM));

  // group
  THDGroup group = workerChannel->newGroup({1, 2});
  int_tensor->resize({1, 2, 3, 4, 5});
  int_tensor->fill(10);
  workerChannel->allReduce(*int_tensor, THDReduceOp::THDReduceSUM, group);
  if (id == 1 || id == 2) {
    for (int i = 0; i < int_tensor->numel(); i++) {
      assert(reinterpret_cast<int*>(int_tensor->data())[i] == 20);
    }

    // rank 0 (master) is not part of group, we cannot perform reduce to it
    try {
      workerChannel->reduce(*int_tensor, THDReduceOp::THDReduceSUM, 0, group);
      assert(false);
    } catch (const std::logic_error& e) {}
  }

  int_tensor->resize({1, 2, 3, 4, 5});
  int_tensor->fill(10);
  workerChannel->reduce(*int_tensor, THDReduceOp::THDReduceSUM, 1, group);
  for (int i = 0; i < int_tensor->numel(); i++) {
    assert(reinterpret_cast<int*>(int_tensor->data())[i] == (id == 1 ? 20 : 10));
  }

  int_tensor->resize({1, 2, 3, 4, 5});
  int_tensor->fill(10);
  if (id == 1)
    int_tensor->fill(2000);

  workerChannel->broadcast(*int_tensor, 1, group);
  if (id == 1 || id == 2) {
    for (int i = 0; i < int_tensor->numel(); i++) {
      assert(reinterpret_cast<int*>(int_tensor->data())[i] == 2000);
    }
  }

  delete float_tensor;
  delete int_tensor;
}


int main() {
  // start master
  std::thread master_thread(master);

  // start worker
  for (int id = 1; id <= WORKERS_NUM; ++id) {
    g_all_workers.push_back(std::thread(worker, id));
  }

  master_thread.join();
  std::cout << "OK" << std::endl;
  return 0;
}
