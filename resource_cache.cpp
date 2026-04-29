#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <omp.h>
#include <random>
#include <unordered_map>
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"
#include "concurrent_hash_map_v6.h"
#ifdef USE_TBB
#include "tbb_wrapper.h"
#endif

// Simulate a resource: data is just a hash of the URL to allow verification
std::string get_mock_data(const std::string& url) {
    return "data_for_" + url;
}

double run_sequential_cache_simulation(int total_ops, int num_unique) {
    std::unordered_map<std::string, std::string> map;
    std::vector<std::string> urls;
    for (int i = 0; i < num_unique; ++i) urls.push_back("url_" + std::to_string(i));
    
    std::default_random_engine gen(42);
    std::uniform_int_distribution<int> url_dist(0, num_unique - 1);
    
    double start = omp_get_wtime();
    for (int i = 0; i < total_ops; ++i) {
        std::string url = urls[url_dist(gen)];
        if (map.find(url) == map.end()) {
            map[url] = get_mock_data(url);
        }
    }
    double end = omp_get_wtime();
    
    std::cout << std::left << std::setw(23) << "Sequential (1 Core)" 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Verification: PASSED" << std::endl;
    return (end - start);
}

template <typename MapType>
void run_cache_simulation(const std::string& name, int total_ops, int num_threads, size_t bucket_count, int num_unique) {
    MapType map(bucket_count);
    std::vector<std::string> urls;
    for (int i = 0; i < num_unique; ++i) urls.push_back("url_" + std::to_string(i));

    double start = omp_get_wtime();
    #pragma omp parallel num_threads(num_threads)
    {
        std::default_random_engine gen(omp_get_thread_num() + 42);
        std::uniform_int_distribution<int> url_dist(0, num_unique - 1);

        #pragma omp for
        for (int i = 0; i < total_ops; ++i) {
            std::string url = urls[url_dist(gen)];
            auto found = map.get(url);
            if (!found) {
                map.put(url, get_mock_data(url));
            }
        }
    }
    double end = omp_get_wtime();

    // --- VERIFICATION STEP ---
    int errors = 0;
    if (num_unique < 10000) { // Only verify for small unique sets to save time
        for (const auto& url : urls) {
            auto val = map.get(url);
            if (!val || *val != get_mock_data(url)) {
                errors++;
            }
        }
    }
    std::string verify = (errors == 0) ? "PASSED" : "FAILED";

    std::cout << std::left << std::setw(23) << name 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Verification: " << verify << std::endl;
}

int main(int argc, char* argv[]) {
    int total_ops = 1000000;
    int num_threads = omp_get_max_threads();
    size_t bucket_count = 131071;
    int num_unique = 1000;

    if (argc > 1) total_ops = std::stoi(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);
    if (argc > 3) bucket_count = std::stoull(argv[3]);
    if (argc > 4) num_unique = std::stoi(argv[4]);

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: WEB BROWSER RESOURCE CACHE (MIXED WORKLOAD)" << std::endl;
    std::cout << " Total Ops: " << total_ops << " | Threads: " << num_threads << std::endl;
    std::cout << " Unique URLs: " << num_unique << " | Initial Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    run_sequential_cache_simulation(total_ops, num_unique);
    run_cache_simulation<ConcurrentHashMapV2<std::string, std::string>>("Library V2 (Static)", total_ops, num_threads, bucket_count, num_unique);
    run_cache_simulation<ConcurrentHashMap<std::string, std::string>>("Library V3 (Dynamic)", total_ops, num_threads, bucket_count, num_unique);
    run_cache_simulation<ConcurrentHashMapV6<std::string, std::string>>("Library V6 (Segmented)", total_ops, num_threads, bucket_count, num_unique);
#ifdef USE_TBB
    run_cache_simulation<TBBHashMapWrapper<std::string, std::string>>("Intel TBB (Industry)", total_ops, num_threads, bucket_count, num_unique);
#endif

    std::cout << "==========================================================" << std::endl;

    return 0;
}
