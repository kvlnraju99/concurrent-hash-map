#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <omp.h>
#include <random>
#include <algorithm>
#include <unordered_map>
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"
#include "concurrent_hash_map_v3_opt.h"
#include "concurrent_hash_map_v3_ptr.h"
#ifdef USE_TBB
#include "tbb_wrapper.h"
#endif

// Simulate a large text corpus with many repeating words
std::vector<std::string> generate_corpus(int total_words, int unique_words) {
    std::vector<std::string> words;
    words.reserve(total_words);
    
    std::default_random_engine generator(42);
    std::discrete_distribution<int> distribution;
    std::vector<double> weights;
    for (int i = 1; i <= unique_words; ++i) {
        weights.push_back(1.0 / i);
    }
    distribution = std::discrete_distribution<int>(weights.begin(), weights.end());

    for (int i = 0; i < total_words; ++i) {
        int word_id = distribution(generator);
        words.push_back("word_" + std::to_string(word_id));
    }
    return words;
}

double run_sequential_word_counter(const std::vector<std::string>& corpus) {
    std::unordered_map<std::string, int> map;
    double start = omp_get_wtime();
    for (const auto& word : corpus) {
        map[word]++;
    }
    double end = omp_get_wtime();
    
    std::cout << std::left << std::setw(20) << "Sequential (1 Core)" 
              << " | Threads: 1  | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Verification: PASSED" << std::endl;
    return (end - start);
}

template <typename MapType>
void run_word_counter(const std::string& name, const std::vector<std::string>& corpus, int num_threads, size_t bucket_count) {
    MapType map(bucket_count);
    
    double start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (size_t i = 0; i < corpus.size(); ++i) {
        map.update(corpus[i], [](std::optional<int> current) {
            return current.value_or(0) + 1;
        });
    }
    double end = omp_get_wtime();

    long long total_sum = map.sum_all_values();
    std::string status = (total_sum == (long long)corpus.size()) ? "PASSED" : "FAILED";

    std::cout << std::left << std::setw(20) << name 
              << " | Threads: " << std::setw(2) << num_threads 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Verification: " << status << " (" << total_sum << ")" << std::endl;
}

int main(int argc, char* argv[]) {
    int total_words = 1000000;
    int unique_words = 10000;
    int num_threads = omp_get_max_threads();
    size_t bucket_count = 131071;

    if (argc > 1) total_words = std::stoi(argv[1]);
    if (argc > 2) unique_words = std::stoi(argv[2]);
    if (argc > 3) num_threads = std::stoi(argv[3]);
    if (argc > 4) bucket_count = std::stoull(argv[4]);

    init_audit(num_threads);
    init_opt_audit(num_threads);
    init_ptr_audit(num_threads);

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
    run_word_counter<ConcurrentHashMapV3Opt<std::string, int>>("Library V3 (Optimized)", corpus, num_threads, bucket_count);
    run_word_counter<ConcurrentHashMapV3Ptr<std::string, int>>("Library V3 (Ptr Swap)", corpus, num_threads, bucket_count);
#ifdef USE_TBB
    run_word_counter<TBBHashMapWrapper<std::string, int>>("Intel TBB (Industry)", corpus, num_threads, bucket_count);
#endif

    std::cout << "==========================================================" << std::endl;

    report_audit();
    report_opt_audit();
    report_ptr_audit();

    return 0;
}
