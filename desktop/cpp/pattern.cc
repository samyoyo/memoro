
#include "pattern.h"
#include <vector>

namespace autopsy {

using namespace std;
/*Unused = 0x1,
  WriteOnly = 1 << 1,
  ReadOnly = 1 << 2,
  ShortLifetime = 1 << 3,
  LateFree = 1 << 4,
  EarlyAlloc =  1 << 5,
  IncreasingReallocs = 1 << 6,
  TopPercentile = 1 << 7*/

bool HasInefficiency(uint64_t bitvec, Inefficiency i) {
  return bitvec & i;
}

float UsageScore(std::vector<Chunk*> const& chunks) {
  double sum = 0;
  uint64_t total_bytes = 0;
  for (auto chunk : chunks) {
    if (chunk->num_writes == 0 && chunk->num_reads == 0)
      continue;
    sum += double(chunk->access_interval_high - chunk->access_interval_low);
    total_bytes += chunk->size;
  }
  if (sum == 0)
    return 0;
  return float(sum) / float(total_bytes);
}

uint64_t Detect(std::vector<Chunk*> const& chunks, PatternParams& params) {

  uint64_t min_lifetime = UINT64_MAX;
  unsigned int total_reads = 0, total_writes = 0;
  bool has_early_alloc = false, has_late_free = false;
  bool has_multi_thread = false;
  bool has_low_access_coverage = false;
  uint64_t last_size;
  unsigned int current_run = 0, longest_run = 0;

  for (auto chunk : chunks) {
    // min lifetime
    uint64_t diff = chunk->timestamp_end - chunk->timestamp_start;
    if (diff < min_lifetime)
      min_lifetime = diff;

    // total reads, writes
    total_reads += chunk->num_reads;
    total_writes += chunk->num_writes;

    // late free
    if (chunk->timestamp_first_access - chunk->timestamp_start >
            (chunk->timestamp_end - chunk->timestamp_start) / 2)
      has_early_alloc = true;

    // early alloc
    if (chunk->timestamp_end - chunk->timestamp_last_access >
        (chunk->timestamp_end - chunk->timestamp_start) / 2) 
      has_late_free = true;

    // increasing alloc sizes
    if (last_size == 0) {
      last_size = chunk->size;
      current_run++;
    } else {
      if (chunk->size >= last_size) {
        last_size = chunk->size;
        current_run++;
      } else {
        longest_run = current_run > longest_run ? current_run : longest_run;
        current_run = 0;
        last_size = chunk->size;
      }
    }
    // multithread
    if (chunk->multi_thread)
      has_multi_thread = true;

    if (float(chunk->access_interval_high - chunk->access_interval_low) / float(chunk->size) < params.access_coverage)
      has_low_access_coverage = true;

  }

  uint64_t i = 0;
  if (min_lifetime <= params.short_lifetime)
    i |= Inefficiency::ShortLifetime;
  if (total_reads == 0 || total_writes == 0) {
    if (total_writes > 0) {
      i |= Inefficiency::WriteOnly;
    } else if (total_reads > 0) {
      i |= Inefficiency::ReadOnly;
    } else {
      i |= Inefficiency::Unused;
    }
  }

  if (has_early_alloc) {
    i |= Inefficiency::EarlyAlloc;
  }
  if (has_late_free) {
    i |= Inefficiency::LateFree;
  }

  if (longest_run >= params.alloc_min_run) {
    i |= Inefficiency::IncreasingReallocs;
  }
  if (has_multi_thread) {
    i |= Inefficiency::MultiThread;
  }
  if (has_low_access_coverage) {
    i |= Inefficiency::LowAccessCoverage;
  }

  return i;
}

void CalculatePercentilesChunk(std::vector<Trace>& traces, PatternParams& params) {
 
  float percentile = params.percentile;
  int index = int(percentile * traces.size());
  for (unsigned int i = index; i < traces.size(); i++) {
    traces[i].inefficiencies |= Inefficiency::TopPercentileChunks;
  }
}

void CalculatePercentilesSize(std::vector<Trace>& traces, PatternParams& params) {

  // we need a different sort order but cannot mutate traces like this
  // since it will invalidate chunk parent indexes
  typedef pair<uint64_t, unsigned int> TVal;
  vector<TVal> t;
  t.reserve(traces.size());
  for (int i = 0; i < traces.size(); i++) {
    t.push_back({traces[i].max_aggregate, i});
  }
  std::sort(t.begin(), t.end(), [](TVal const& a, TVal const& b) {
      return a.first < b.first;
      });

  float percentile = params.percentile;
  int index = int(percentile * traces.size());
  for (unsigned int i = index; i < t.size(); i++) {
    traces[t[i].second].inefficiencies |= Inefficiency::TopPercentileSize;
  }
}

}

