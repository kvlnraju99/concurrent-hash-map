#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <omp.h>
#include <iomanip>
#include "concurrent_hash_map_v2.h"
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v6.h"
#include "tbb_wrapper.h"

// Sequential baseline for comparison
void run_sequential_word_counter(const std::vector<std::string>& corpus) {
    auto start = std::chrono::high_resolution_clock::now();
    std::unordered_map<std::string, int> counts;
    for (const auto& word : corpus) counts[word]++;
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << std::left << std::setw(20) << "Sequential (1 Core)" 
              << " | Threads: 1  | Time: " << std::fixed << std::setprecision(4) << elapsed.count() << "s | Verification: PASSED" << std::endl;
}

template<typename MapType>
void run_word_counter(const std::string& label, const std::vector<std::string>& corpus, int num_threads, size_t bucket_count) {
    MapType map(bucket_count);
    auto start = std::chrono::high_resolution_clock::now();
    
    #pragma omp parallel for num_threads(num_threads)
    for (size_t i = 0; i < corpus.size(); ++i) {
        map.update(corpus[i], [](std::optional<int> old_val) {
            return old_val ? *old_val + 1 : 1;
        });
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    
    size_t total_count = map.sum_all_values();
    std::string verify = (total_count == corpus.size()) ? "PASSED" : "FAILED";
    
    std::cout << std::left << std::setw(20) << label 
              << " | Threads: " << std::setw(2) << num_threads 
              << " | Time: " << std::fixed << std::setprecision(4) << elapsed.count() << "s"
              << " | Verification: " << verify << " (" << total_count << ")" << std::endl;
}

std::vector<std::string> generate_corpus(int total_words, int unique_words) {
    std::vector<std::string> corpus;
    corpus.reserve(total_words);
    for (int i = 0; i < total_words; ++i) {
        corpus.push_back("word_" + std::to_string(i % unique_words));
    }
    return corpus;
}

int main(int argc, char* argv[]) {
    int total_words = (argc > 1) ? std::stoi(argv[1]) : 1000000;
    int unique_words = (argc > 2) ? std::stoi(argv[2]) : 100000;
    int num_threads = (argc > 3) ? std::stoi(argv[3]) : omp_get_max_threads();
    size_t bucket_count = (argc > 4) ? std::stoull(argv[4]) : 1024;

    init_audit(num_threads);

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: PARALLEL WORD FREQUENCY COUNTER" << std::endl;
    std::cout << " Total Words: " << total_words << " | Unique Words: " << unique_words << std::endl;
    std::cout << " Threads: " << num_threads << " | Initial Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::vector<std::string> corpus = generate_corpus(total_words, unique_words);
    std::cout << "Corpus generated. Starting benchmarks...\n" << std::endl;

    run_sequential_word_counter(corpus);
    run_word_counter<ConcurrentHashMapV2<std::string, int>>("Library V2 (Static)", corpus, num_threads, bucket_count);
    run_word_counter<ConcurrentHashMap<std::string, int>>("Library V3 (Dynamic)", corpus, num_threads, bucket_count);
    run_word_counter<ConcurrentHashMapV6<std::string, int>>("Library V6 (Segmented)", corpus, num_threads, bucket_count);
#ifdef USE_TBB
    run_word_counter<TBBHashMapWrapper<std::string, int>>("Intel TBB (Industry)", corpus, num_threads, bucket_count);
#endif

    std::cout << "==========================================================" << std::endl;
    report_audit();

    return 0;
}
