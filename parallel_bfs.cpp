#include <iostream>
#include <vector>
#include <queue>
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

// Simple Graph Structure: Adjacency List
struct Graph {
    int num_nodes;
    std::vector<std::vector<int>> adj;
    Graph(int n) : num_nodes(n), adj(n) {}
};

Graph generate_random_graph(int num_nodes, int edges_per_node) {
    Graph g(num_nodes);
    std::default_random_engine gen(42);
    std::uniform_int_distribution<int> dist(0, num_nodes - 1);
    for (int i = 0; i < num_nodes; ++i) {
        for (int j = 0; j < edges_per_node; ++j) {
            int neighbor = dist(gen);
            if (neighbor != i) g.adj[i].push_back(neighbor);
        }
    }
    return g;
}

double run_sequential_bfs(const Graph& g, int start_node) {
    std::unordered_map<int, bool> visited;
    std::queue<int> q;
    
    double start = omp_get_wtime();
    q.push(start_node);
    visited[start_node] = true;
    
    while (!q.empty()) {
        int curr = q.front();
        q.pop();
        for (int neighbor : g.adj[curr]) {
            if (visited.find(neighbor) == visited.end()) {
                visited[neighbor] = true;
                q.push(neighbor);
            }
        }
    }
    double end = omp_get_wtime();
    
    std::cout << std::left << std::setw(23) << "Sequential (1 Core)" 
              << " | Threads: 1  | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Visited: " << visited.size() << std::endl;
    return (end - start);
}

template <typename MapType>
void run_bfs(const std::string& name, const Graph& g, int start_node, int num_threads, size_t bucket_count) {
    MapType visited(bucket_count);
    std::vector<int> current_frontier;
    current_frontier.push_back(start_node);
    visited.put(start_node, true);

    double start = omp_get_wtime();
    while (!current_frontier.empty()) {
        std::vector<int> next_frontier;
        #pragma omp parallel num_threads(num_threads)
        {
            std::vector<int> local_frontier;
            #pragma omp for nowait
            for (size_t i = 0; i < current_frontier.size(); ++i) {
                int curr = current_frontier[i];
                for (int neighbor : g.adj[curr]) {
                    bool already_visited = false;
                    auto found = visited.get(neighbor);
                    if (found) already_visited = true;

                    if (!already_visited) {
                        visited.put(neighbor, true);
                        local_frontier.push_back(neighbor);
                    }
                }
            }
            #pragma omp critical
            next_frontier.insert(next_frontier.end(), local_frontier.begin(), local_frontier.end());
        }
        current_frontier = std::move(next_frontier);
    }
    double end = omp_get_wtime();

    std::cout << std::left << std::setw(23) << name 
              << " | Threads: " << std::setw(2) << num_threads 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s"
              << " | Visited: " << visited.size() << std::endl;
}

int main(int argc, char* argv[]) {
    int num_nodes = 100000;
    int edges_per_node = 10;
    int num_threads = omp_get_max_threads();
    size_t bucket_count = 131071;

    if (argc > 1) num_nodes = std::stoi(argv[1]);
    if (argc > 2) edges_per_node = std::stoi(argv[2]);
    if (argc > 3) num_threads = std::stoi(argv[3]);
    if (argc > 4) bucket_count = std::stoull(argv[4]);

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: PARALLEL GRAPH BFS" << std::endl;
    std::cout << " Nodes: " << num_nodes << " | Edges/Node: " << edges_per_node << std::endl;
    std::cout << " Threads: " << num_threads << " | Initial Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::cout << "Generating random graph..." << std::endl;
    Graph g = generate_random_graph(num_nodes, edges_per_node);
    std::cout << "Graph generated. Starting BFS benchmarks...\n" << std::endl;

    run_sequential_bfs(g, 0);
    run_bfs<ConcurrentHashMapV2<int, bool>>("Library V2 (Static)", g, 0, num_threads, bucket_count);
    run_bfs<ConcurrentHashMap<int, bool>>("Library V3 (Dynamic)", g, 0, num_threads, bucket_count);
    run_bfs<ConcurrentHashMapV6<int, bool>>("Library V6 (Segmented)", g, 0, num_threads, bucket_count);
#ifdef USE_TBB
    run_bfs<TBBHashMapWrapper<int, bool>>("Intel TBB (Industry)", g, 0, num_threads, bucket_count);
#endif

    std::cout << "==========================================================" << std::endl;

    return 0;
}
