/*
 * viewpoint_planner_data.cpp
 *
 *  Created on: Dec 24, 2016
 *      Author: bhepp
 */

#include "viewpoint_planner_data.h"
#include <boost/filesystem.hpp>

const std::string NodeObject::kFileTag = "NodeObject";

ViewpointPlannerData::ViewpointPlannerData(const Options* options) {
  roi_bbox_ = BoundingBoxType(
      Vector3(options->getValue<FloatType>("roi_bbox_min_x"),
          options->getValue<FloatType>("roi_bbox_min_y"),
          options->getValue<FloatType>("roi_bbox_min_z")),
      Vector3(options->getValue<FloatType>("roi_bbox_max_x"),
          options->getValue<FloatType>("roi_bbox_max_y"),
          options->getValue<FloatType>("roi_bbox_max_z")));
  drone_extent_ = Vector3(
      options->getValue<FloatType>("drone_extent_x"),
      options->getValue<FloatType>("drone_extent_y"),
      options->getValue<FloatType>("drone_extent_z"));
  grid_dimension_ = options->getValue<size_t>("grid_dimension");
  df_cutoff_ = options->getValue<FloatType>("distance_field_cutoff");

  const std::string reconstruction_path = options->getValue<std::string>("dense_reconstruction_path");
  const std::string raw_octree_filename = options->getValue<std::string>("raw_octree_filename");
  const std::string mesh_filename = options->getValue<std::string>("poisson_mesh_filename");
  std::string octree_filename = options->getValue<std::string>("octree_filename");
  if (octree_filename.empty()) {
    octree_filename = raw_octree_filename + ".aug";
  }
  std::string bvh_filename = options->getValue<std::string>("bvh_filename");
  if (bvh_filename.empty()) {
    bvh_filename = octree_filename + ".bvh";
  }
  std::string df_filename = options->getValue<std::string>("distance_field_filename");
  if (df_filename.empty()) {
    df_filename = mesh_filename + ".df";
  }

  readPoissonMesh(mesh_filename);
  readDenseReconstruction(reconstruction_path);
  bool augmented_octree_generated = readAndAugmentOctree(octree_filename, raw_octree_filename);
  bool bvh_generated = readBVHTree(bvh_filename, octree_filename);
  bool df_generated = readMeshDistanceField(df_filename, mesh_filename);
  if (augmented_octree_generated || bvh_generated || df_generated) {
    updateWeights();
    std::cout << "Writing updated augmented octree" << std::endl;
    octree_->write(octree_filename);
    std::cout << "Writing updated BVH tree" << std::endl;
    writeBVHTree(bvh_filename);
  }
}

void ViewpointPlannerData::readDenseReconstruction(const std::string& path) {
  std::cout << "Reading dense reconstruction workspace" << std::endl;
  ait::Timer timer;
  reconstruction_.reset(new DenseReconstruction());
  reconstruction_->read(path);
  timer.printTiming("Loading dense reconstruction");
}

std::unique_ptr<ViewpointPlannerData::RawOccupancyMapType>
ViewpointPlannerData::readRawOctree(const std::string& octree_filename, bool binary) const {
  ait::Timer timer;
  std::unique_ptr<RawOccupancyMapType> raw_octree;
  if (binary) {
//      octree.reset(new ViewpointPlanner::OccupancyMapType(filename));
    throw AIT_EXCEPTION("Binary occupancy maps not supported");
  }
  else {
    raw_octree = RawOccupancyMapType::read(octree_filename);
  }
  if (!raw_octree) {
    throw std::runtime_error("Unable to read octomap file");
  }
  timer.printTiming("Loading octree");

  std::cout << "Loaded octree" << std::endl;
  std::cout << "Octree has " << raw_octree->getNumLeafNodes() << " leaf nodes and " << raw_octree->size() << " total nodes" << std::endl;
  std::cout << "Metric extents:" << std::endl;
  double x, y, z;
  timer = ait::Timer();
  raw_octree->getMetricSize(x, y, z);
  timer.printTiming("Computing octree size");
  std::cout << "  size=(" << x << ", " << y << ", " << z << ")" << std::endl;
  timer = ait::Timer();
  raw_octree->getMetricMin(x, y, z);
  timer.printTiming("Computing octree min");
  std::cout << "   min=(" << x << ", " << y << ", " << z << ")" << std::endl;
  timer = ait::Timer();
  raw_octree->getMetricMax(x, y, z);
  timer.printTiming("Computing octree max");
  std::cout << "   max=(" << x << ", " << y << ", " << z << ")" << std::endl;

  size_t count_unknown = 0;
  size_t count_unknown_leaf = 0;
  for (auto it = raw_octree->begin_tree(); it != raw_octree->end_tree(); ++it) {
    if (it->getObservationCount() == 0) {
      ++count_unknown;
      if (it.isLeaf()) {
        ++count_unknown_leaf;
      }
    }
  }
  std::cout << "Unknown voxels: " << count_unknown << std::endl;
  std::cout << "Unknown leaf voxels: " << count_unknown_leaf << std::endl;
  return std::move(raw_octree);
}

void ViewpointPlannerData::readPoissonMesh(const std::string& mesh_filename) {
  // Read poisson reconstructed mesh
  poisson_mesh_.reset(new ViewpointPlannerData::MeshType);
  ViewpointPlannerData::MeshIOType::loadFromFile(mesh_filename, *poisson_mesh_.get());
  std::cout << "Number of triangles in mesh: " << poisson_mesh_->m_FaceIndicesVertices.size() << std::endl;
}

bool ViewpointPlannerData::readBVHTree(std::string bvh_filename, const std::string& octree_filename) {
  // Read cached BVH tree (if up-to-date) or generate it
  bool read_cached_tree = false;
  if (boost::filesystem::exists(bvh_filename)) {
    if (boost::filesystem::last_write_time(bvh_filename) > boost::filesystem::last_write_time(octree_filename)) {
      std::cout << "Loading up-to-date cached BVH tree." << std::endl;
      readCachedBVHTree(bvh_filename);
      read_cached_tree = true;
    }
    else {
      std::cout << "Found cached BVH tree to be old. Ignoring it." << std::endl;
    }
  }
  if (!read_cached_tree) {
    std::cout << "Generating BVH tree." << std::endl;
    generateBVHTree(octree_.get());
    writeBVHTree(bvh_filename);
  }
  std::cout << "BVH tree bounding box: " << occupied_bvh_.getRoot()->getBoundingBox() << std::endl;
  return !read_cached_tree;
}

bool ViewpointPlannerData::readMeshDistanceField(std::string df_filename, const std::string& mesh_filename) {
  // Read cached distance field (if up-to-date) or generate it.
  bool read_cached_df = false;
  if (boost::filesystem::exists(df_filename)) {
    if (boost::filesystem::last_write_time(df_filename) > boost::filesystem::last_write_time(mesh_filename)) {
      std::cout << "Loading up-to-date cached distance field." << std::endl;
      ml::BinaryDataStreamFile file_stream(df_filename, false);
      file_stream >> distance_field_;
      read_cached_df = true;
    }
    else {
      std::cout << "Found cached BVH tree to be old. Ignoring it." << std::endl;
    }
  }
  if (!read_cached_df) {
    std::cout << "Generating distance field." << std::endl;
    generateDistanceField();
    ml::BinaryDataStreamFile file_stream(df_filename, true);
    file_stream << distance_field_;
  }
  std::cout << "BVH tree bounding box: " << occupied_bvh_.getRoot()->getBoundingBox() << std::endl;
  return !read_cached_df;
}

void ViewpointPlannerData::updateWeights() {
  for (auto it = octree_->begin_tree(); it != octree_->end_tree(); ++it) {
    it->setWeight(0);
  }
//  FloatType min_distance = std::numeric_limits<FloatType>::max();
  FloatType max_distance = std::numeric_limits<FloatType>::lowest();
  for (int x = 0; x < grid_dim_(0); ++x) {
    for (int y = 0; y < grid_dim_(1); ++y) {
      for (int z = 0; z < grid_dim_(2); ++z) {
//        min_distance = std::min(min_distance, distance_field_(x, y, z));
        max_distance = std::max(max_distance, distance_field_(x, y, z));
      }
    }
  }
//  std::cout << "min_distance: " << min_distance << std::endl;
//  std::cout << "max_distance: " << max_distance << std::endl;
//  FloatType max_weight = std::numeric_limits<FloatType>::lowest();
//  FloatType min_weight = std::numeric_limits<FloatType>::max();
  for (int x = 0; x < grid_dim_(0); ++x) {
    for (int y = 0; y < grid_dim_(1); ++y) {
      for (int z = 0; z < grid_dim_(2); ++z) {
        FloatType distance = distance_field_(x, y, z);
        FloatType inv_distance = (max_distance - distance) / (FloatType)max_distance;
        FloatType weight = inv_distance * inv_distance;
//        max_weight = std::max(max_weight, weight);
//        min_weight = std::min(min_weight, weight);
        Vector3 pos = getGridPosition(x, y, z);
        BoundingBoxType bbox(pos, grid_increment_);
        std::vector<OccupiedTreeType::BBoxIntersectionResult> results =
            occupied_bvh_.intersects(bbox);
        for (const OccupiedTreeType::BBoxIntersectionResult& result : results) {
          result.node->getObject()->weight = weight;
        }
        octomap::point3d oct_min(bbox.getMinimum(0), bbox.getMinimum(1), bbox.getMinimum(2));
        octomap::point3d oct_max(bbox.getMaximum(0), bbox.getMaximum(1), bbox.getMaximum(2));
        for (auto it = octree_->begin_leafs_bbx(oct_min, oct_max); it != octree_->end_leafs_bbx(); ++it) {
          it->setWeight(weight);
        }
      }
    }
  }
//  std::cout << "min_weight: " << min_weight<< std::endl;
//  std::cout << "max_weight: " << max_weight<< std::endl;
  octree_->updateInnerOccupancy();
}

bool ViewpointPlannerData::readAndAugmentOctree(
    std::string octree_filename, const std::string& raw_octree_filename, bool binary) {
  // Read cached augmented tree (if up-to-date) or generate it
  bool read_cached_tree = false;
  if (boost::filesystem::exists(octree_filename)) {
    if (boost::filesystem::last_write_time(octree_filename) > boost::filesystem::last_write_time(raw_octree_filename)) {
      std::cout << "Loading up-to-date cached augmented tree [" << octree_filename << "]" << std::endl;
      octree_ = OccupancyMapType::read(octree_filename);
      read_cached_tree = true;
    }
    else {
      std::cout << "Found cached augmented tree to be old. Ignoring it." << std::endl;
    }
  }
  if (!read_cached_tree) {
    std::cout << "Reading non-augmented input tree [" << raw_octree_filename << "]" << std::endl;
    std::unique_ptr<RawOccupancyMapType> raw_octree = readRawOctree(raw_octree_filename, binary);
    std::cout << "Generating augmented tree." << std::endl;
    octree_ = generateAugmentedOctree(std::move(raw_octree));
    octree_->write(octree_filename);
  }
  return !read_cached_tree;
}

std::unique_ptr<ViewpointPlannerData::OccupancyMapType>
ViewpointPlannerData::generateAugmentedOctree(std::unique_ptr<RawOccupancyMapType> raw_octree) const {
  ait::Timer timer;
  if (!isTreeConsistent(*raw_octree.get())) {
    throw AIT_EXCEPTION("Input tree is inconsistent");
  }

  std::unique_ptr<OccupancyMapType> output_tree(convertToAugmentedMap(raw_octree.get()));

  if (!isTreeConsistent(*output_tree.get())) {
    throw AIT_EXCEPTION("Augmented tree is inconsistent");
  }
  timer.printTimingMs("Copying input tree");

  timer = ait::Timer();
  // Augment tree with weights
  std::vector<TreeNavigatorType> query_nodes;
  AIT_ASSERT(OCCUPANCY_WEIGHT_DEPTH - OCCUPANCY_WEIGHT_REACH > 0);
  AIT_ASSERT(OCCUPANCY_WEIGHT_DEPTH_CUTOFF > OCCUPANCY_WEIGHT_DEPTH);
  for (auto it = output_tree->begin_tree(OCCUPANCY_WEIGHT_DEPTH); it != output_tree->end_tree(); ++it) {
    if (it.getDepth() == OCCUPANCY_WEIGHT_DEPTH) {
      query_nodes.push_back(TreeNavigatorType(output_tree.get(), it.getKey(), &(*it), it.getDepth()));
    }
  }

  float max_total_weight = 0;
  for (const TreeNavigatorType& query_nav : query_nodes) {
    const float dist_cutoff = 0.5f * query_nav.getSize();
    const float dist_cutoff_sq = dist_cutoff * dist_cutoff;
    const Eigen::Vector3f query_pos = query_nav.getPosition();

    ConstTreeNavigatorType parent_nav = query_nav;
    for (size_t i = 0; i < OCCUPANCY_WEIGHT_REACH; ++i) {
      parent_nav.gotoParent();
    }
    std::stack<ConstTreeNavigatorType> node_stack;
    node_stack.push(parent_nav);

    WeightType total_weight = 0;
    size_t total_count = 0;
    while (!node_stack.empty()) {
      ConstTreeNavigatorType nav = node_stack.top();
      node_stack.pop();
      if (nav.hasChildren() && nav.getDepth() < OCCUPANCY_WEIGHT_DEPTH_CUTOFF) {
        for (size_t i = 0; i < 8; ++i) {
          if (nav.hasChild(i)) {
            node_stack.push(nav.child(i));
          }
        }
      }
      else {
        if (nav->getObservationCount() > 0 && output_tree->isNodeOccupied(nav.getNode())) {
//        if (nav->getObservationCount() > 0) {
          // TODO
//          WeightType weight = computeWeightContribution(query_pos, dist_cutoff_sq, nav);
          WeightType weight = 0;
//          size_t count;
//          WeightType weight;
//          if (nav->hasChildren()) {
//            size_t count = 1 << (output_tree->getTreeDepth() - nav.getDepth());
//            weight = nav->getMeanChildOccupancy() * count;
//          } else {
//            count = 1;
//            weight = nav->getOccupancy();
//          }
//          total_count += count;
          total_weight += weight;
        }
      }
    }

//    total_weight = total_count > 0 ? total_weight / total_count : 0;
//    std::cout << "total_weight=" << total_weight << ", total_count=" << total_count << std::endl;

//    std::cout << "total_weight: " << total_weight << std::endl;
    if (total_weight > max_total_weight) {
      max_total_weight = total_weight;
    }

    // Pass weight down the tree to the leaf nodes
    std::stack<TreeNavigatorType> node_stack2;
    node_stack2.push(query_nav);
    while (!node_stack2.empty()) {
      TreeNavigatorType nav = node_stack2.top();
      node_stack2.pop();
      AIT_ASSERT(nav->getWeight() == 0);
      nav->setWeight(total_weight);
      if (nav.hasChildren()) {
        for (size_t i = 0; i < 8; ++i) {
          if (nav.hasChild(i)) {
            node_stack2.push(nav.child(i));
          }
        }
      }
    }
  }
  timer.printTimingMs("Augmenting tree");

  std::cout << "Maximum weight: " << max_total_weight << std::endl;
  return std::move(output_tree);
}

template <typename TreeT>
bool ViewpointPlannerData::isTreeConsistent(const TreeT& tree) {
  bool consistent = true;
  for (auto it = tree.begin_tree(); it != tree.end_tree(); ++it) {
    if (it->hasChildren()) {
      for (size_t i = 0; i < 8; ++i) {
        if (it->hasChild(i)) {
          if (it->getObservationCount() > it->getChild(i)->getObservationCount()) {
            consistent = false;
            break;
          }
          if (it->getOccupancy() < it->getChild(i)->getOccupancy()) {
            consistent = false;
            break;
          }
        }
      }
      if (!consistent) {
        break;
      }
    }
  }
  return consistent;
}

void ViewpointPlannerData::generateBVHTree(const OccupancyMapType* octree) {
  std::vector<typename OccupiedTreeType::ObjectWithBoundingBox> objects;
  for (auto it = octree->begin_tree(); it != octree->end_tree(); ++it) {
    if (it.isLeaf()) {
//      if (octree->isNodeFree(&(*it)) || octree->isNodeUnknown(&(*it))) {
      if (octree->isNodeFree(&(*it)) && octree->isNodeKnown(&(*it))) {
        continue;
      }
      typename OccupiedTreeType::ObjectWithBoundingBox object_with_bbox;
      octomap::point3d center_octomap = it.getCoordinate();
      Eigen::Vector3f center;
      center << center_octomap.x(), center_octomap.y(), center_octomap.z();
      const float size = it.getSize();
      object_with_bbox.bounding_box = typename OccupiedTreeType::BoundingBoxType(center, size);
      object_with_bbox.object = new NodeObject();
      object_with_bbox.object->occupancy = it->getOccupancy();
      object_with_bbox.object->observation_count = it->getObservationCount();
      object_with_bbox.object->weight = it->getWeight();
      object_with_bbox.bounding_box.constrainTo(roi_bbox_);
      if (object_with_bbox.bounding_box.isEmpty()) {
        continue;
      }
      objects.push_back(object_with_bbox);
    }
  }
  std::cout << "Building BVH tree with " << objects.size() << " objects" << std::endl;
  ait::Timer timer;
  occupied_bvh_.build(std::move(objects));
  timer.printTimingMs("Building BVH tree");
}

void ViewpointPlannerData::writeBVHTree(const std::string& filename) const {
  occupied_bvh_.write(filename);
}

void ViewpointPlannerData::readCachedBVHTree(const std::string& filename) {
  occupied_bvh_.read(filename);
}

void ViewpointPlannerData::generateDistanceField() {
  std::cout << "Generating mesh distance field" << std::endl;
  ml::Grid3f seed_grid(grid_dimension_, grid_dimension_, grid_dimension_);
  seed_grid.setValues(std::numeric_limits<float>::max());
  BoundingBoxType bbox = occupied_bvh_.getRoot()->getBoundingBox();
  grid_dim_ = Vector3i(grid_dimension_, grid_dimension_, grid_dimension_);
  grid_origin_ = bbox.getMinimum();
  grid_increment_ = bbox.getMaxExtent() / (grid_dimension_ + 1);
  Vector3 grid_max = grid_origin_ + Vector3(grid_increment_, grid_increment_, grid_increment_) * (grid_dimension_ + 1);
  grid_bbox_ = BoundingBoxType(grid_origin_, grid_max);
  //  std::cout << "grid_dim_: " << grid_dim_ << std::endl;
  //  std::cout << "grid_origin_: " << grid_origin_ << std::endl;
  //  std::cout << "grid_increment_: " << grid_increment_ << std::endl;
  //  std::cout << "grid_max: " << grid_max << std::endl;
  //  std::cout << "grid_bbox_: " << grid_bbox_ << std::endl;
  for (size_t i = 0; i < poisson_mesh_->m_FaceIndicesVertices.size(); ++i) {
    const MeshType::Indices::Face& face = poisson_mesh_->m_FaceIndicesVertices[i];
    AIT_ASSERT_STR(face.size() == 3, "Mesh faces need to have a valence of 3");
    const ml::vec3f& vertex1 = poisson_mesh_->m_Vertices[face[0]];
    const ml::vec3f& vertex2 = poisson_mesh_->m_Vertices[face[1]];
    const ml::vec3f& vertex3 = poisson_mesh_->m_Vertices[face[2]];
    const ml::vec3f ml_tri_xyz = (vertex1 + vertex2 + vertex3) / 3;
    const Vector3 tri_xyz(ml_tri_xyz.x, ml_tri_xyz.y, ml_tri_xyz.z);
    if (isInsideGrid(tri_xyz)) {
      const Vector3i indices = getGridIndices(tri_xyz);
      const Vector3 xyz = getGridPosition(indices);
      const float cur_dist = seed_grid(indices(0), indices(1), indices(2));
      const float new_dist = (xyz - tri_xyz).norm() / grid_increment_;
      if (new_dist < cur_dist) {
        // TODO: Use zero or proper distance
        seed_grid(indices(0), indices(1), indices(2)) = new_dist;
//        seed_grid(indices(0), indices(1), indices(2)) = 0;
      }
    }
  }
  distance_field_ = DistanceFieldType(seed_grid);
  for (int x = 0; x < grid_dim_(0); ++x) {
    for (int y = 0; y < grid_dim_(1); ++y) {
      for (int z = 0; z < grid_dim_(2); ++z) {
        if (distance_field_(x, y, z) > df_cutoff_) {
          distance_field_(x, y, z) = df_cutoff_;
        }
      }
    }
  }
  std::cout << "Done" << std::endl;
}

bool ViewpointPlannerData::isInsideGrid(const Vector3& xyz) const {
  return grid_bbox_.isInside(xyz);
}

ViewpointPlannerData::Vector3i ViewpointPlannerData::getGridIndices(const Vector3& xyz) const {
  Vector3 indices_float = (xyz - grid_origin_) / grid_increment_;
  Vector3i indices(Eigen::round(indices_float.array()).cast<int>());
  for (int i = 0; i < indices.rows(); ++i) {
    if (indices(i) >= grid_dim_(i) && xyz(i) < grid_bbox_.getMaximum()(i)) {
      --indices(i);
    }
  }
  return indices;
}

ViewpointPlannerData::Vector3 ViewpointPlannerData::getGridPosition(const Vector3i& indices) const {
  return grid_origin_ + grid_increment_ * indices.cast<float>();
}

ViewpointPlannerData::Vector3 ViewpointPlannerData::getGridPosition(int ix, int iy, int iz) const {
  return getGridPosition(Vector3i(ix, iy, iz));
}

ViewpointPlannerData::WeightType ViewpointPlannerData::computeWeightContribution(
    const Eigen::Vector3f& query_pos, float dist_cutoff_sq, const ConstTreeNavigatorType& nav) {
//  const WeightType observation_factor = computeObservationWeightFactor(nav->getObservationCountSum());
//  WeightType weight = nav->getOccupancy() * observation_factor;
  WeightType weight = nav->getOccupancy() * nav->getObservationCountSum();
  const Eigen::Vector3f node_pos = nav.getPosition();
  float dist_sq = (node_pos - query_pos).squaredNorm();
  dist_sq = std::max(dist_cutoff_sq, dist_sq);
  weight /= std::sqrt(dist_cutoff_sq / dist_sq);
  return weight;
}