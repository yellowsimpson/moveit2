/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2016, CITEC, Bielefeld University
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Robert Haschke */

#include <moveit_setup_srdf_plugins/collision_matrix_model.hpp>
#include <boost/assign.hpp>
#include <QVector>
#include <QBrush>
#include <QColor>
#include <QPalette>
#include <QApplication>
#include <QItemSelection>
#include <unordered_map>

namespace moveit_setup
{
namespace srdf_setup
{
/// Mapping of reasons to a background color
static const std::unordered_map<DisabledReason, QVariant> LONG_REASONS_TO_BRUSH =
    boost::assign::map_list_of  // clang-format off
    ( NEVER, QBrush(QColor("lightgreen")) )
    ( DEFAULT, QBrush(QColor("lightpink")) )
    ( ADJACENT, QBrush(QColor("powderblue")) )
    ( ALWAYS, QBrush(QColor("tomato")) )
    ( USER, QBrush(QColor("yellow")) )
    ( NOT_DISABLED, QBrush());  // clang-format on

CollisionMatrixModel::CollisionMatrixModel(const srdf::SRDFWriter& srdf, const std::vector<std::string>& names,
                                           QObject* parent)
  : QAbstractTableModel(parent), srdf_(srdf), std_names_(names)
{
  int idx = 0;
  for (std::vector<std::string>::const_iterator it = names.begin(), end = names.end(); it != end; ++it, ++idx)
  {
    visual_to_index_ << idx;
    q_names_ << QString::fromStdString(*it);
  }
}

int CollisionMatrixModel::rowCount(const QModelIndex& /*parent*/) const
{
  return visual_to_index_.size();
}

int CollisionMatrixModel::columnCount(const QModelIndex& /*parent*/) const
{
  return visual_to_index_.size();
}

struct PairMatcher
{
  PairMatcher(const std::string& link1, const std::string& link2)
    : search(link1 < link2 ? std::make_pair(std::cref(link1), std::cref(link2)) :
                             std::make_pair(std::cref(link2), std::cref(link1)))
  {
  }

  bool operator()(const srdf::Model::CollisionPair& pair) const
  {
    return (pair.link1_ == search.first && pair.link2_ == search.second) ||
           (pair.link2_ == search.first && pair.link1_ == search.second);
  }

  std::pair<const std::string&, const std::string&> search;
};

template <typename Container>
auto find(Container& pairs, const std::string& link1, const std::string& link2)
{
  return std::find_if(pairs.begin(), pairs.end(), PairMatcher(link1, link2));
}

bool CollisionMatrixModel::disabledByDefault(const std::string& link1, const std::string& link2) const
{
  for (const auto& name : srdf_.no_default_collision_links_)
  {
    if (name == link1 || name == link2)
    {
      return true;
    }
  }
  return false;
}

QVariant CollisionMatrixModel::data(const QModelIndex& index, int role) const
{
  static std::string enabled = "Explicitly enabled";
  static std::string disabled = "Disabled by default";
  static QBrush default_collision_brush(QColor("lightpink").darker(110));

  if (index.isValid() && index.row() == index.column())
  {
    switch (role)
    {
      case Qt::BackgroundRole:
        return QApplication::palette().window();
      default:
        return QVariant();
    }
  }

  const std::string* reason = nullptr;
  int r = visual_to_index_[index.row()], c = visual_to_index_[index.column()];
  auto it = find(srdf_.disabled_collision_pairs_, std_names_[r], std_names_[c]);
  if (it != srdf_.disabled_collision_pairs_.end())
  {
    reason = &it->reason_;
  }
  else if (find(srdf_.enabled_collision_pairs_, std_names_[r], std_names_[c]) != srdf_.enabled_collision_pairs_.end())
  {
    reason = &enabled;
  }
  else if (disabledByDefault(std_names_[r], std_names_[c]))
  {
    reason = &disabled;
  }

  switch (role)
  {
    case Qt::CheckStateRole:
      return (!reason || reason == &enabled) ? Qt::Unchecked : Qt::Checked;
    case Qt::ToolTipRole:
      return reason ? QString::fromStdString(*reason) : QString();
    case Qt::BackgroundRole:
      if (!reason || reason == &enabled)
      {
        return QVariant();
      }
      else if (reason == &disabled)
      {
        return default_collision_brush;
      }
      else
      {
        return LONG_REASONS_TO_BRUSH.at(moveit_setup::srdf_setup::disabledReasonFromString(*reason));
      }
  }
  return QVariant();
}

bool CollisionMatrixModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
  if (role != Qt::CheckStateRole)
    return false;

  bool new_value = (value.toInt() == Qt::Checked);
  srdf::Model::CollisionPair p{ std_names_[visual_to_index_[index.row()]], std_names_[visual_to_index_[index.column()]],
                                std::string() };
  if (p.link1_ > p.link2_)
    std::swap(p.link1_, p.link2_);

  auto enabled = find(srdf_.enabled_collision_pairs_, p.link1_, p.link2_);
  auto disabled = find(srdf_.disabled_collision_pairs_, p.link1_, p.link2_);
  bool changed = true;
  if (disabledByDefault(p.link1_, p.link2_))
  {
    assert(disabled == srdf_.disabled_collision_pairs_.end());
    auto& pairs = srdf_.enabled_collision_pairs_;
    if (new_value)
    {
      if (enabled != pairs.end())  // delete all matching pairs, starting with enabled
      {
        pairs.erase(std::remove_if(enabled, pairs.end(), PairMatcher(p.link1_, p.link2_)), pairs.end());
      }
      else
      {
        changed = false;
      }
    }
    else
    {
      p.reason_ = moveit_setup::srdf_setup::disabledReasonToString(moveit_setup::srdf_setup::NOT_DISABLED);
      if (enabled == pairs.end())
      {
        srdf_.enabled_collision_pairs_.push_back(p);
      }
      else
      {
        changed = false;
      }
    }
  }
  else
  {
    assert(enabled == srdf_.enabled_collision_pairs_.end());
    auto& pairs = srdf_.disabled_collision_pairs_;
    if (new_value)
    {
      p.reason_ = moveit_setup::srdf_setup::disabledReasonToString(moveit_setup::srdf_setup::USER);
      if (disabled == pairs.end())
      {
        pairs.push_back(p);
      }
      else
      {
        changed = false;
      }
    }
    else
    {
      if (disabled != pairs.end())  // delete all matching pairs, starting with disabled
      {
        pairs.erase(std::remove_if(disabled, pairs.end(), PairMatcher(p.link1_, p.link2_)), pairs.end());
      }
      else
      {
        changed = false;
      }
    }
  }

  if (changed)
  {
    QModelIndex mirror = this->index(index.column(), index.row());
    Q_EMIT dataChanged(index, index);
    Q_EMIT dataChanged(mirror, mirror);
  }
  return changed;
}

void CollisionMatrixModel::setEnabled(const QItemSelection& selection, bool value)
{
  // perform changes without signalling
  QItemSelection changes;
  blockSignals(true);
  for (const auto& range : selection)
  {
    setEnabled(range.indexes(), value);

    const QModelIndex& top_left = range.topLeft();
    const QModelIndex& bottom_right = range.bottomRight();
    changes.select(top_left, bottom_right);
    changes.select(createIndex(top_left.column(), top_left.row()),
                   createIndex(bottom_right.column(), bottom_right.row()));
  }
  blockSignals(false);

  // emit changes
  for (const auto& range : changes)
    Q_EMIT dataChanged(range.topLeft(), range.bottomRight());
}

void CollisionMatrixModel::setEnabled(const QModelIndexList& indexes, bool value)
{
  for (const auto idx : indexes)
    setData(idx, value ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
}

void CollisionMatrixModel::setFilterRegExp(const QString& filter)
{
  beginResetModel();
  QRegExp regexp(filter);
  visual_to_index_.clear();
  for (int idx = 0, end = q_names_.size(); idx != end; ++idx)
  {
    if (q_names_[idx].contains(regexp))
      visual_to_index_ << idx;
  }
  endResetModel();
}

QVariant CollisionMatrixModel::headerData(int section, Qt::Orientation /*orientation*/, int role) const
{
  if (role == Qt::DisplayRole)
    return q_names_[visual_to_index_[section]];
  return QVariant();
}

Qt::ItemFlags CollisionMatrixModel::flags(const QModelIndex& index) const
{
  if (!index.isValid())
    return Qt::NoItemFlags;

  Qt::ItemFlags f = QAbstractTableModel::flags(index);
  if (index.row() != index.column())
    f |= Qt::ItemIsUserCheckable;
  return f;
}
}  // namespace srdf_setup
}  // namespace moveit_setup
