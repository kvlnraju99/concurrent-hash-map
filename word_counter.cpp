#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <omp.h>
#include <random>
#include <algorithm>
#include "naive_map.h"
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"

// Simulate a large text corpus with many repeating words
std::vector<std::string> generate_corpus(int total_words, int unique_words) {
    std::vector<std::string> words;
    words.reserve(total_words);
    
    // Zipf-like distribution: small IDs appear much more frequently
    std::default_random_engine generator(42);
    std::discrete_distribution<int> distribution;
    std::vector<double> weights;
    for (int i = 1; i <= unique_words; ++i) {
        weights.push_back(1.0 / i); // Zipf's Law: 1/n frequency
    }
    distribution = std::discrete_distribution<int>(weights.begin(), weights.end());

    for (int i = 0; i < total_words; ++i) {
        int word_id = distribution(generator);
        words.push_back("word_" + std::to_string(word_id));
    }
    return words;
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

    std::cout << std::left << std::setw(20) << name 
              << " | Threads: " << std::setw(2) << num_threads 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s" << std::endl;
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

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: PARALLEL WORD FREQUENCY COUNTER" << std::endl;
    std::cout << " Total Words: " << total_words << " | Unique Words: " << unique_words << std::endl;
    std::cout << " Threads: " << num_threads << " | Initial Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::vector<std::string> corpus = generate_corpus(total_words, unique_words);
    std::cout << "Corpus generated. Starting benchmarks...\n" << std::endl;

    run_word_counter<NaiveHashMap<std::string, int>>("Naive (Global)", corpus, num_threads, bucket_count);
    run_word_counter<ConcurrentHashMapV2<std::string, int>>("Library V2 (Static)", corpus, num_threads, bucket_count);
    run_word_counter<ConcurrentHashMap<std::string, int>>("Library V3 (Dynamic)", corpus, num_threads, bucket_count);

    std::cout << "==========================================================" << std::endl;

    return 0;
}
