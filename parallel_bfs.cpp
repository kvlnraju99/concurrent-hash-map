#include <iostream>
#include <vector>
#include <queue>
#include <iomanip>
#include <omp.h>
#include <random>
#include "naive_map.h"
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"
#include "concurrent_hash_map_v4.h"
#include "concurrent_hash_map_v5.h"
#ifdef USE_TBB
#include "tbb_wrapper.h"
#endif

// Simple Graph Structure: Adjacency List
struct Graph {
    int num_nodes;
    std::vector<std::vector<int>> adj;

    Graph(int n) : num_nodes(n), adj(n) {}

    void add_edge(int u, int v) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    }
};

// Generate a random Erdős–Rényi graph
Graph generate_random_graph(int n, int edges_per_node) {
    Graph g(n);
    std::default_random_engine generator(42);
    std::uniform_int_distribution<int> distribution(0, n - 1);

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < edges_per_node; ++j) {
            int v = distribution(generator);
            if (i != v) g.add_edge(i, v);
        }
    }
    return g;
}

template <typename MapType>
void run_bfs(const std::string& name, const Graph& g, int start_node, int num_threads, size_t bucket_count) {
    MapType visited(bucket_count);
    std::vector<int> frontier;
    frontier.push_back(start_node);
    visited.put(start_node, true);

    double start_time = omp_get_wtime();

    while (!frontier.empty()) {
        std::vector<int> next_frontier;
        
        // Parallelize the exploration of the current frontier
        #pragma omp parallel num_threads(num_threads)
        {
            std::vector<int> local_next;
            #pragma omp for nowait
            for (int i = 0; i < (int)frontier.size(); ++i) {
                int u = frontier[i];
                for (int v : g.adj[u]) {
                    bool already_visited = false;
                    // Atomically check and mark visited
                    visited.update(v, [&](std::optional<bool> v_status) {
                        if (v_status.has_value()) {
                            already_visited = true;
                            return true;
                        } else {
                            already_visited = false;
                            return true;
                        }
                    });

                    if (!already_visited) {
                        local_next.push_back(v);
                    }
                }
            }

            // Combine local frontiers into the global next_frontier
            #pragma omp critical
            {
                next_frontier.insert(next_frontier.end(), local_next.begin(), local_next.end());
            }
        }
        frontier = std::move(next_frontier);
    }

    double end_time = omp_get_wtime();
    
    // Verification Step
    size_t visited_count = visited.size();

    std::cout << std::left << std::setw(20) << name 
              << " | Threads: " << std::setw(2) << num_threads 
              << " | Time: " << std::fixed << std::setprecision(4) << (end_time - start_time) << "s"
              << " | Visited: " << visited_count << std::endl;
}

int main(int argc, char* argv[]) {
    int nodes = 100000;
    int edges_per_node = 20;
    int num_threads = omp_get_max_threads();
    size_t bucket_count = 131071;

    if (argc > 1) nodes = std::stoi(argv[1]);
    if (argc > 2) edges_per_node = std::stoi(argv[2]);
    if (argc > 3) num_threads = std::stoi(argv[3]);
    if (argc > 4) bucket_count = std::stoull(argv[4]);

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: PARALLEL GRAPH BFS" << std::endl;
    std::cout << " Nodes: " << nodes << " | Edges/Node: " << edges_per_node << std::endl;
    std::cout << " Threads: " << num_threads << " | Initial Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::cout << "Generating random graph...\n";
    Graph g = generate_random_graph(nodes, edges_per_node);
    std::cout << "Graph generated. Starting BFS benchmarks...\n\n";

    run_bfs<NaiveHashMap<int, bool>>("Naive (Global)", g, 0, num_threads, bucket_count);
    run_bfs<ConcurrentHashMapV2<int, bool>>("Library V2 (Static)", g, 0, num_threads, bucket_count);
    run_bfs<ConcurrentHashMap<int, bool>>("Library V3 (Dynamic)", g, 0, num_threads, bucket_count);
    // run_bfs<ConcurrentHashMapV4<int, bool>>("Library V4 (Atomic)", g, 0, num_threads, bucket_count);
    // run_bfs<ConcurrentHashMapV5<int, bool>>("Library V5 (Wait-Free)", g, 0, num_threads, bucket_count);
#ifdef USE_TBB
    run_bfs<TBBHashMapWrapper<int, bool>>("Intel TBB (Industry)", g, 0, num_threads, bucket_count);
#endif

    std::cout << "==========================================================" << std::endl;

    return 0;
}
