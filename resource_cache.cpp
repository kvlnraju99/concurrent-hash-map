#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <omp.h>
#include <random>
#include "naive_map.h"
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"

// Simulate a resource: data is just a hash of the URL to allow verification
std::string get_mock_data(const std::string& url) {
    return "data_for_" + url;
}

template <typename MapType>
void run_cache_simulation(const std::string& name, int total_ops, int num_threads, size_t bucket_count) {
    MapType cache(bucket_count);
    
    // We'll have 20% writers and 80% readers
    int num_writers = std::max(1, num_threads / 5);
    int num_readers = num_threads - num_writers;

    std::atomic<int> errors{0};
    double start = omp_get_wtime();

    #pragma omp parallel num_threads(num_threads)
    {
        int tid = omp_get_thread_num();
        bool is_writer = (tid < num_writers);
        
        std::default_random_engine gen(42 + tid);
        std::uniform_int_distribution<int> url_dist(0, 10000); // 10k possible resources

        int ops_per_thread = total_ops / num_threads;

        for (int i = 0; i < ops_per_thread; ++i) {
            std::string url = "http://example.com/res_" + std::to_string(url_dist(gen));
            
            if (is_writer) {
                // Writer: "Download" and cache the resource
                cache.put(url, get_mock_data(url));
            } else {
                // Reader: "Request" the resource from cache
                auto data = cache.get(url);
                if (data) {
                    if (*data != get_mock_data(url)) {
                        errors++;
                    }
                }
            }
        }
    }

    double end = omp_get_wtime();
    double throughput = total_ops / (end - start);

    std::cout << std::left << std::setw(20) << name 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Ops/sec: " << std::fixed << std::setprecision(0) << throughput
              << " | Errors: " << errors.load() << std::endl;
}

int main(int argc, char* argv[]) {
    int total_ops = 2000000;
    int num_threads = omp_get_max_threads();
    size_t bucket_count = 131071;

    if (argc > 1) total_ops = std::stoi(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);
    if (argc > 3) bucket_count = std::stoull(argv[3]);

    int num_writers = std::max(1, num_threads / 5);
    int num_readers = num_threads - num_writers;

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: WEB BROWSER RESOURCE CACHE (MIXED WORKLOAD)" << std::endl;
    std::cout << " Total Ops: " << total_ops << " | Threads: " << num_threads << std::endl;
    std::cout << " Role Split: " << num_writers << " Writers, " << num_readers << " Readers" << std::endl;
    std::cout << " Initial Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    run_cache_simulation<NaiveHashMap<std::string, std::string>>("Naive (Global)", total_ops, num_threads, bucket_count);
    run_cache_simulation<ConcurrentHashMapV2<std::string, std::string>>("Library V2 (Static)", total_ops, num_threads, bucket_count);
    run_cache_simulation<ConcurrentHashMap<std::string, std::string>>("Library V3 (Dynamic)", total_ops, num_threads, bucket_count);

    std::cout << "==========================================================" << std::endl;

    return 0;
}
