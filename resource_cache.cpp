#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <omp.h>
#include <random>
#include <unordered_map>
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"
#ifdef USE_TBB
#include "tbb_wrapper.h"
#endif

// Simulate a resource: data is just a hash of the URL to allow verification
std::string get_mock_data(const std::string& url) {
    return "data_for_" + url;
}

double run_sequential_cache_simulation(int total_ops) {
    std::unordered_map<std::string, std::string> map;
    std::vector<std::string> urls;
    for (int i = 0; i < 1000; ++i) urls.push_back("url_" + std::to_string(i));
    
    std::default_random_engine gen(42);
    std::uniform_int_distribution<int> url_dist(0, 999);
    
    double start = omp_get_wtime();
    for (int i = 0; i < total_ops; ++i) {
        std::string url = urls[url_dist(gen)];
        if (map.find(url) == map.end()) {
            map[url] = get_mock_data(url);
        }
    }
    double end = omp_get_wtime();
    
    std::cout << std::left << std::setw(20) << "Sequential (1 Core)" 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Ops/sec: " << (int)(total_ops / (end - start)) << std::endl;
    return (end - start);
}

template <typename MapType>
void run_cache_simulation(const std::string& name, int total_ops, int num_threads, size_t bucket_count) {
    MapType map(bucket_count);
    std::vector<std::string> urls;
    for (int i = 0; i < 1000; ++i) urls.push_back("url_" + std::to_string(i));

    double start = omp_get_wtime();
    #pragma omp parallel num_threads(num_threads)
    {
        std::default_random_engine gen(omp_get_thread_num() + 42);
        std::uniform_int_distribution<int> op_dist(0, 99);
        std::uniform_int_distribution<int> url_dist(0, 999);

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

    std::cout << std::left << std::setw(20) << name 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Ops/sec: " << (int)(total_ops / (end - start)) << std::endl;
}

int main(int argc, char* argv[]) {
    int total_ops = 1000000;
    int num_threads = omp_get_max_threads();
    size_t bucket_count = 131071;

    if (argc > 1) total_ops = std::stoi(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);
    if (argc > 3) bucket_count = std::stoull(argv[3]);

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: WEB BROWSER RESOURCE CACHE (MIXED WORKLOAD)" << std::endl;
    std::cout << " Total Ops: " << total_ops << " | Threads: " << num_threads << std::endl;
    std::cout << " Initial Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    run_sequential_cache_simulation(total_ops);
    run_cache_simulation<ConcurrentHashMapV2<std::string, std::string>>("Library V2 (Static)", total_ops, num_threads, bucket_count);
    run_cache_simulation<ConcurrentHashMap<std::string, std::string>>("Library V3 (Dynamic)", total_ops, num_threads, bucket_count);
#ifdef USE_TBB
    run_cache_simulation<TBBHashMapWrapper<std::string, std::string>>("Intel TBB (Industry)", total_ops, num_threads, bucket_count);
#endif

    std::cout << "==========================================================" << std::endl;

    return 0;
}
