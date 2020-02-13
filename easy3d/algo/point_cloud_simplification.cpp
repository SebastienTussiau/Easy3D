/**
 * Copyright (C) 2015 by Liangliang Nan (liangliang.nan@gmail.com)
 * https://3d.bk.tudelft.nl/liangliang/
 *
 * This file is part of Easy3D. If it is useful in your research/work,
 * I would be grateful if you show your appreciation by citing it:
 * ------------------------------------------------------------------
 *      Liangliang Nan.
 *      Easy3D: a lightweight, easy-to-use, and efficient C++
 *      library for processing and rendering 3D data. 2018.
 * ------------------------------------------------------------------
 * Easy3D is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 3
 * as published by the Free Software Foundation.
 *
 * Easy3D is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <easy3d/algo/point_cloud_simplification.h>

#include <cassert>

#include <easy3d/core/point_cloud.h>
#include <easy3d/util/logging.h>
#include <easy3d/util/stop_watch.h>
#include <easy3d/kdtree/kdtree_search_eth.h>


namespace easy3d {

    double PointCloudSimplification::average_spacing(PointCloud *cloud, KdTreeSearch *kdtree, int k/* = 6*/,
                                                   bool accurate /* = false*/, int samples /* = 200000*/) {
        double total = 0;
        const std::vector<vec3> &points = cloud->points();
        int num = cloud->n_vertices();

        StopWatch t;
        t.start();
        LOG(INFO) << "computing average spacing ...";

//        ProgressLogger progress(cloud->n_vertices());
        int step = 1;
        if (!accurate && num > samples)
            step = num / samples;
        int count = 0;
        for (int i = 0; i < num; i += step) {
            const vec3 &p = points[i];
            std::vector<int> neighbors;
            std::vector<float> sqr_distances;
            kdtree->find_closest_K_points(p, k + 1, neighbors, sqr_distances);  // k+1 to exclude itself
            if (neighbors.size() <= 1) {// in case we get less than k+1 neighbors
                continue;
            }

            double avg = 0;
            for (unsigned int i = 1; i < sqr_distances.size(); ++i) { // starts from 1 to exclude itself
                avg += std::sqrt(sqr_distances[i]);
            }

            total += (avg / neighbors.size());
            ++count;
//            progress.next();
        }

        LOG(INFO) << "done. time: " << t.time_string();
        return (total / count);
    }


    double PointCloudSimplification::average_spacing(PointCloud *cloud, int k /* = 6 */, bool accurate /* = false */,
                                                   int samples /* = 200000 */) {
        StopWatch t;
        t.start();
        LOG(INFO) << "building kd-tree ...";
        KdTreeSearch_ETH kdtree;
        kdtree.begin();
        kdtree.add_point_cloud(cloud);
        kdtree.end();
        LOG(INFO) << "done. time: " << t.time_string();

        return average_spacing(cloud, &kdtree, k, accurate, samples);
    }


    namespace internal {
        /// Utility class for grid simplification of point set.
        /// LessEpsilonPoints defines a 3D points order: two points are equal
        /// iff they belong to the same cell of a grid of cell size = epsilon.
        template<class Point>
        class LessEpsilonPoints {
        public:
            LessEpsilonPoints(float epsilon) : m_epsilon(epsilon) {
                assert(epsilon > 0);
            }

            // Round points to multiples of m_epsilon, then compare.
            bool operator()(const Point &a, const Point &b) const {
                const vec3 *a_n = (a.pos);
                const vec3 *b_n = (b.pos);

                vec3 rounded_a(round_epsilon(a_n->x, m_epsilon), round_epsilon(a_n->y, m_epsilon),
                               round_epsilon(a_n->z, m_epsilon));
                vec3 rounded_b(round_epsilon(b_n->x, m_epsilon), round_epsilon(b_n->y, m_epsilon),
                               round_epsilon(b_n->z, m_epsilon));

                //return (rounded_a < rounded_b);
                if (rounded_a.x < rounded_b.x)
                    return true;
                else if (rounded_a.x == rounded_b.x) {
                    if (rounded_a.y < rounded_b.y)
                        return true;
                    else if (rounded_a.y == rounded_b.y) {
                        if (rounded_a.z < rounded_b.z)
                            return true;
                    }
                }

                return false;
            }

        private:
            // Round number to multiples of epsilon
            static inline float round_epsilon(float value, float epsilon) {
                return std::floor(value / epsilon) * epsilon;
            }

        private:
            float m_epsilon;
        };
    }


//////////////////////////////////////////////////////////////////////////


    std::vector<int> PointCloudSimplification::grid_simplification(PointCloud *cloud, float epsilon) {
        assert(epsilon > 0);

        struct Point {
            const vec3 *pos;
            std::size_t idx;
        };

        LOG(INFO) << "querying points ...";

        StopWatch t;
        t.start();

//        ProgressLogger progress(cloud->n_vertices());

        // Merges points which belong to the same cell of a grid of cell size = epsilon.
        // points_to_keep will contain 1 point per cell; the others will be in points_to_remove.
        std::set<Point, internal::LessEpsilonPoints<Point> > points_to_keep(epsilon);
        std::vector<int> points_to_remove;

        const std::vector<vec3> &points = cloud->points();
        for (std::size_t i = 0; i < points.size(); ++i) {
            Point p;
            p.pos = &(points[i]);
            p.idx = i;
            std::pair<std::set<Point, internal::LessEpsilonPoints<Point> >::iterator, bool> result = points_to_keep.insert(
                    p);
            if (!result.second) // if not inserted
                points_to_remove.push_back(int(p.idx));

//            progress.next();
        }

        LOG(INFO) << "done. time: " << t.time_string();
        return points_to_remove;
    }


    std::vector<int>
    PointCloudSimplification::uniform_simplification(PointCloud *cloud, KdTreeSearch *kdtree, float epsilon) {
        LOG(INFO) << "querying points ...";

        StopWatch t;
        t.start();

        std::vector<char> keep(cloud->n_vertices(), 1);
        const std::vector<vec3> &points = cloud->points();

//        ProgressLogger progress(cloud->n_vertices());
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (keep[i]) {
                const vec3 &p = points[i];
                std::vector<int> neighbors;
                kdtree->find_points_in_radius(p, epsilon, neighbors);
                if (neighbors.size() > 1) {
                    for (std::size_t j = 1; j < neighbors.size(); ++j) {
                        int idx = neighbors[j];
                        keep[idx] = 0;
                    }
                }
            }
//            progress.next();
        }

        std::vector<int> points_to_remove;
        for (std::size_t i = 0; i < keep.size(); ++i) {
            if (!keep[i])
                points_to_remove.push_back(int(i));
        }

        LOG(INFO) << "done. time: " << t.time_string();
        return points_to_remove;
    }


    std::vector<int> PointCloudSimplification::uniform_simplification(PointCloud *cloud, float epsilon) {
        StopWatch t;
        t.start();
        LOG(INFO) << "building kd-tree ...";
        KdTreeSearch_ETH kdtree;
        kdtree.begin();
        kdtree.add_point_cloud(cloud);
        kdtree.end();
        LOG(INFO) << "done. time: " << t.time_string();

        return uniform_simplification(cloud, &kdtree, epsilon);
    }


//----- uniform simplification (specifying expected point number) ---------------------------------


    namespace internal {
        struct PointPair {
            PointPair(unsigned int idx_a, unsigned int idx_b, float dist) : distance(dist) {
                if (idx_a < idx_b) {
                    index_a = idx_a;
                    index_b = idx_b;
                } else {
                    index_a = idx_b;
                    index_b = idx_a;
                }
            }

            unsigned int index_a;
            unsigned int index_b;
            float distance;
        };

        class LessDistPointPair {
        public:
            LessDistPointPair() {}

            bool operator()(const PointPair &pair_a, const PointPair &pair_b) const {
                return pair_a.distance < pair_b.distance;
            }
        };

        // Only one pass. After execution, some points are deleted, but the number of remaining points may 
        // still be greater than the expected number. 
        // NOTE: the returned indices are w.r.t. the new point cloud (a subset of the original point cloud). 
        static std::vector<int> uniform_simplification(PointCloud *cloud, unsigned int expected_num) {
            const std::vector<vec3> &points = cloud->points();
            unsigned int num = cloud->n_vertices();

            std::vector<int> points_to_delete;
            if (expected_num >= num)
                return points_to_delete;    // expected num is greater than / equal to given number.

            KdTreeSearch_ETH kdtree;
            kdtree.begin();
            kdtree.add_point_cloud(cloud);
            kdtree.end();

            // the average squared distance to its nearest neighbor; smaller value means highter density
            std::vector<float> sqr_distance(cloud->n_vertices());
            std::set<internal::PointPair, internal::LessDistPointPair> point_pairs;
            for (unsigned int i = 0; i < num; ++i) {
                const vec3 &p = points[i];
                std::vector<int> neighbors;
                std::vector<float> sqr_dists;
                kdtree.find_closest_K_points(p, 2, neighbors, sqr_dists); // the first one is itself
                if (neighbors.size() == 2) {
                    sqr_distance[i] = sqr_dists[1];

                    // now we get a pair of points
                    internal::PointPair pair(i, neighbors[1], sqr_dists[1]);
                    assert(i == neighbors[0]);
                    assert(i != neighbors[1]);
                    point_pairs.insert(pair);
                } else {
                    // ignore, no point will not be deleted
                }
            }

            // Now the elements in point_pairs are sorted in an increasing order, so we can delete points now.
            unsigned int remaining_num = num;
            std::set<internal::PointPair, internal::LessDistPointPair>::iterator pos = point_pairs.begin();
            std::set<internal::PointPair, internal::LessDistPointPair>::iterator end = point_pairs.end();
            for (; pos != end; ++pos) {
                if (remaining_num == expected_num)
                    break;
                const internal::PointPair &pair = *pos;
                unsigned int id_a = pair.index_a;
                unsigned int id_b = pair.index_b;
                if (sqr_distance[id_a] < sqr_distance[id_b])
                    points_to_delete.push_back(id_a);
                else
                    points_to_delete.push_back(id_b);
                --remaining_num;
            }

            return points_to_delete;
        }
    }


    std::vector<int> PointCloudSimplification::uniform_simplification(PointCloud *cloud, unsigned int num_expected) {
        std::vector<int> points_to_delete;

        int num_original = cloud->n_vertices();
        int num_should_delete = num_original - num_expected;
        if (num_should_delete <= 0)
            return points_to_delete;

        std::vector<char> remain(cloud->n_vertices(), 1);    // 1: keep this point; 0: delete this point
        const std::vector<vec3> &points = cloud->points();

        //---------------------------------------------------------------

        std::vector<unsigned int> original_index(num_original);
        for (int i = 0; i < num_original; ++i)
            original_index[i] = i;

        PointCloud *point_cloud = cloud;
        bool cloud_is_new = false;

//        ProgressLogger progress(num_should_delete);
        while (points_to_delete.size() < num_should_delete) {
            const std::vector<int> &to_delete = internal::uniform_simplification(point_cloud, num_expected);

            for (std::size_t i = 0; i < to_delete.size(); ++i) {
                int new_id = to_delete[i];
                int orig_id = original_index[new_id];
                if (remain[orig_id]) {
                    points_to_delete.push_back(orig_id);
                    remain[orig_id] = 0;
                }
            }
//            progress.notify(points_to_delete.size());

            // Now we have some points to delete, but the remaining point number may still be greater than the expected number. 
            // We create a new point cloud from the remaining points and run the same algorithm again. We have to know the original
            // indices of the points in the new point cloud.
            if (cloud_is_new)
                delete point_cloud;

            if (points_to_delete.size() < num_should_delete) {
                point_cloud = new PointCloud;
                cloud_is_new = true;
                std::vector<vec3> &new_points = point_cloud->points();
                original_index.clear(); // we need to update the original_index
                for (unsigned int i = 0; i < remain.size(); ++i) {
                    if (remain[i]) {
                        point_cloud->add_vertex(points[i]);
                        original_index.push_back(i);
                    }
                }
            }
        }

        return points_to_delete;
    }


}