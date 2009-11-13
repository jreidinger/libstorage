/*
 * Copyright (c) [2004-2009] Novell, Inc.
 *
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, contact Novell, Inc.
 *
 * To contact Novell about this file by physical or electronic mail, you may
 * find current contact information at www.novell.com.
 */


#include <string>
#include <fstream>
#include <array>
#include <boost/algorithm/string.hpp>

#include "config.h"
#include "storage/Graph.h"
#include "storage/AppUtil.h"
#include "storage/HumanString.h"
#include "storage/StorageInterface.h"


namespace storage
{
    using namespace std;


    enum NodeType { NODE_DISK, NODE_DMMULTIPATH, NODE_DMRAID, NODE_PARTITION, NODE_MDRAID,
		    NODE_LVMVG, NODE_LVMLV, NODE_DM, NODE_MOUNTPOINT };

    struct Node
    {
	Node(NodeType type, const string& device, const string& label, unsigned long long sizeK)
	    : type(type), device(device), label(label), sizeK(sizeK) {}

	string id() const { return (type == NODE_MOUNTPOINT ? "mountpoint:" : "device:") + device; }

	NodeType type;
	string device;
	string label;
	unsigned long long sizeK;
    };


    enum EdgeType { EDGE_SUBDEVICE, EDGE_MOUNT, EDGE_USED };

    struct Edge
    {
	Edge(EdgeType type, const string& id1, const string& id2)
	    : type(type), id1(id1), id2(id2) {}

	EdgeType type;
	string id1;
	string id2;
    };


    struct Rank
    {
	Rank(NodeType type, const char* name)
	    : type(type), name(name) {}

	NodeType type;
	string name;
    };

    typedef array<Rank, 5> Ranks;
    const Ranks ranks = { { Rank(NODE_DISK, "source"), Rank(NODE_PARTITION, "same"),
			    Rank(NODE_LVMVG, "same"), Rank(NODE_LVMLV, "same"),
			    Rank(NODE_MOUNTPOINT, "sink") } };


    string dotQuote(const string& str)
    {
	return '"' + boost::replace_all_copy(str, "\"", "\\\"") + '"';
    }


    string makeTooltip(const char* text, const string& label, unsigned long long sizeK)
    {
	ostringstream s;
	s << text << "\\n" << label << "\\n" << byteToHumanString(1024 * sizeK, false, 2, false);
	return dotQuote(s.str());
    }


    std::ostream& operator<<(std::ostream& s, const Node& node)
    {
	s << dotQuote(node.id()) << " [label=" << dotQuote(node.label);
	switch (node.type)
	{
	    case NODE_DISK:
		s << ", color=\"#ff0000\", fillcolor=\"#ffaaaa\""
		  << ", tooltip=" << makeTooltip(_("Hard Disk"), node.device, node.sizeK);
		break;
	    case NODE_DMRAID:
		s << ", color=\"#ff0000\", fillcolor=\"#ffaaaa\""
		  << ", tooltip=" << makeTooltip(_("BIOS RAID"), node.device, node.sizeK);
		break;
	    case NODE_DMMULTIPATH:
		s << ", color=\"#ff0000\", fillcolor=\"#ffaaaa\""
		  << ", tooltip=" << makeTooltip(_("Multipath"), node.device, node.sizeK);
		break;
	    case NODE_PARTITION:
		s << ", color=\"#cc33cc\", fillcolor=\"#eeaaee\""
		  << ", tooltip=" << makeTooltip(_("Partition"), node.device, node.sizeK);
		break;
	    case NODE_MDRAID:
		s << ", color=\"#aaaa00\", fillcolor=\"#ffffaa\""
		  << ", tooltip=" << makeTooltip(_("RAID"), node.device, node.sizeK);
		break;
	    case NODE_LVMVG:
		s << ", color=\"#0000ff\", fillcolor=\"#aaaaff\""
		  << ", tooltip=" << makeTooltip(_("Volume Group"), node.device, node.sizeK);
		break;
	    case NODE_LVMLV:
		s << ", color=\"#6622dd\", fillcolor=\"#bb99ff\""
		  << ", tooltip=" << makeTooltip(_("Logical Volume"), node.device, node.sizeK);
		break;
	    case NODE_DM:
		s << ", color=\"#885511\", fillcolor=\"#ddbb99\""
		  << ", tooltip=" << makeTooltip(_("DM Device"), node.device, node.sizeK);
		break;
	    case NODE_MOUNTPOINT:
		s << ", color=\"#008800\", fillcolor=\"#99ee99\""
		  << ", tooltip=" << makeTooltip(_("Mount Point"), node.label, node.sizeK);
		break;
	}
	return s << "];";
    }


    std::ostream& operator<<(std::ostream& s, const Edge& edge)
    {
	s << dotQuote(edge.id1) << " -> " << dotQuote(edge.id2);
	switch (edge.type)
	{
	    case EDGE_SUBDEVICE:
		s << " [color=\"#444444\", style=solid]";
		break;
	    case EDGE_MOUNT:
		s << " [color=\"#444444\", style=dashed]";
		break;
	    case EDGE_USED:
		s << " [color=\"#444444\", style=dotted]";
		break;
	}
	return s << ";";
    }


    bool
    saveGraph(StorageInterface* s, const string& filename)
    {
	list<Node> nodes;
	list<Edge> edges;


	deque<ContainerInfo> containers;
	s->getContainers(containers);
	for (deque<ContainerInfo>::const_iterator i1 = containers.begin(); i1 != containers.end(); ++i1)
	{
	    switch (i1->type)
	    {
		case DISK: {

		    DiskInfo diskinfo;
		    s->getDiskInfo(i1->device, diskinfo);

		    Node disk_node(NODE_DISK, i1->device, i1->name, diskinfo.sizeK);
		    nodes.push_back(disk_node);

		    for (list<UsedByInfo>::const_iterator i2 = i1->usedBy.begin(); i2 != i1->usedBy.end(); ++i2)
		    {
			edges.push_back(Edge(EDGE_USED, disk_node.id(), "device:" + i2->device));
		    }

		    deque<PartitionInfo> partitions;
		    s->getPartitionInfo(i1->name, partitions);
		    for (deque<PartitionInfo>::const_iterator i2 = partitions.begin(); i2 != partitions.end(); ++i2)
		    {
			if (i2->partitionType == EXTENDED)
			    continue;

			Node partition_node(NODE_PARTITION, i2->v.device, i2->v.name, i2->v.sizeK);
			nodes.push_back(partition_node);

			edges.push_back(Edge(EDGE_SUBDEVICE, disk_node.id(), partition_node.id()));

			for (list<UsedByInfo>::const_iterator i3 = i2->v.usedBy.begin(); i3 != i2->v.usedBy.end(); ++i3)
			{
			    edges.push_back(Edge(EDGE_USED, partition_node.id(), "device:" + i3->device));
			}

			if (!i2->v.mount.empty())
			{
			    Node mountpoint_node(NODE_MOUNTPOINT, i2->v.device, i2->v.mount, i2->v.sizeK);
			    nodes.push_back(mountpoint_node);

			    edges.push_back(Edge(EDGE_MOUNT, partition_node.id(), mountpoint_node.id()));
			}
		    }

		} break;

		case LVM: {

		    LvmVgInfo lvmvginfo;
		    s->getLvmVgInfo(i1->name, lvmvginfo);

		    Node vg_node(NODE_LVMVG, i1->device, i1->name, lvmvginfo.sizeK);
		    nodes.push_back(vg_node);

		    deque<LvmLvInfo> lvs;
		    s->getLvmLvInfo(i1->name, lvs);
		    for (deque<LvmLvInfo>::const_iterator i2 = lvs.begin(); i2 != lvs.end(); ++i2)
		    {
			Node lv_node(NODE_LVMLV, i2->v.device, i2->v.name, i2->v.sizeK);
			nodes.push_back(lv_node);

			edges.push_back(Edge(EDGE_SUBDEVICE, vg_node.id(), lv_node.id()));

			for (list<UsedByInfo>::const_iterator i3 = i2->v.usedBy.begin(); i3 != i2->v.usedBy.end(); ++i3)
			{
			    edges.push_back(Edge(EDGE_USED, lv_node.id(), "device:" + i3->device));
			}

			if (!i2->v.mount.empty())
			{
			    Node mountpoint_node(NODE_MOUNTPOINT, i2->v.device, i2->v.mount, i2->v.sizeK);
			    nodes.push_back(mountpoint_node);

			    edges.push_back(Edge(EDGE_MOUNT, lv_node.id(), mountpoint_node.id()));
			}
		    }

		} break;

		case MD: {

		    deque<MdInfo> mds;
		    s->getMdInfo(mds);

		    for (deque<MdInfo>::const_iterator i2 = mds.begin(); i2 != mds.end(); ++i2)
		    {
			Node md_node(NODE_MDRAID, i2->v.device, i2->v.name, i2->v.sizeK);
			nodes.push_back(md_node);

			for (list<UsedByInfo>::const_iterator i3 = i2->v.usedBy.begin(); i3 != i2->v.usedBy.end(); ++i3)
			{
			    edges.push_back(Edge(EDGE_USED, md_node.id(), "device:" + i3->device));
			}

			if (!i2->v.mount.empty())
			{
			    Node mountpoint_node(NODE_MOUNTPOINT, i2->v.device, i2->v.mount, i2->v.sizeK);
			    nodes.push_back(mountpoint_node);

			    edges.push_back(Edge(EDGE_MOUNT, md_node.id(), mountpoint_node.id()));
			}
		    }

		} break;

		case DM: {

		    deque<DmInfo> dms;
		    s->getDmInfo(dms);

		    for (deque<DmInfo>::const_iterator i2 = dms.begin(); i2 != dms.end(); ++i2)
		    {
			Node dm_node(NODE_DM, i2->v.device, i2->v.name, i2->v.sizeK);
			nodes.push_back(dm_node);

			for (list<UsedByInfo>::const_iterator i3 = i2->v.usedBy.begin(); i3 != i2->v.usedBy.end(); ++i3)
			{
			    edges.push_back(Edge(EDGE_USED, dm_node.id(), "device:" + i3->device));
			}

			if (!i2->v.mount.empty())
			{
			    Node mountpoint_node(NODE_MOUNTPOINT, i2->v.device, i2->v.mount, i2->v.sizeK);
			    nodes.push_back(mountpoint_node);

			    edges.push_back(Edge(EDGE_MOUNT, dm_node.id(), mountpoint_node.id()));
			}
		    }

		} break;

		case DMRAID: {

		    DmraidCoInfo dmraidcoinfo;
		    s->getDmraidCoInfo(i1->device, dmraidcoinfo);

		    Node dmraid_node(NODE_DMRAID, i1->device, i1->name, dmraidcoinfo.p.d.sizeK);
		    nodes.push_back(dmraid_node);

		    for (list<UsedByInfo>::const_iterator i2 = i1->usedBy.begin(); i2 != i1->usedBy.end(); ++i2)
		    {
			edges.push_back(Edge(EDGE_USED, dmraid_node.id(), "device:" + i2->device));
		    }

		    deque<DmraidInfo> partitions;
		    s->getDmraidInfo(i1->name, partitions);
		    for (deque<DmraidInfo>::const_iterator i2 = partitions.begin(); i2 != partitions.end(); ++i2)
		    {
			if (i2->p.p.partitionType == EXTENDED)
			    continue;

			Node partition_node(NODE_PARTITION, i2->p.v.device, i2->p.v.name, i2->p.v.sizeK);
			nodes.push_back(partition_node);

			edges.push_back(Edge(EDGE_SUBDEVICE, dmraid_node.id(), partition_node.id()));

			for (list<UsedByInfo>::const_iterator i3 = i2->p.v.usedBy.begin(); i3 != i2->p.v.usedBy.end(); ++i3)
			{
			    edges.push_back(Edge(EDGE_USED, partition_node.id(), "device:" + i3->device));
			}

			if (!i2->p.v.mount.empty())
			{
			    Node mountpoint_node(NODE_MOUNTPOINT, i2->p.v.device, i2->p.v.mount, i2->p.v.sizeK);
			    nodes.push_back(mountpoint_node);

			    edges.push_back(Edge(EDGE_MOUNT, partition_node.id(), mountpoint_node.id()));
			}
		    }

		} break;

		case DMMULTIPATH: {

		    DmmultipathCoInfo dmmultipathcoinfo;
		    s->getDmmultipathCoInfo(i1->device, dmmultipathcoinfo);

		    Node dmmultipath_node(NODE_DMMULTIPATH, i1->device, i1->name, dmmultipathcoinfo.p.d.sizeK);
		    nodes.push_back(dmmultipath_node);

		    for (list<UsedByInfo>::const_iterator i2 = i1->usedBy.begin(); i2 != i1->usedBy.end(); ++i2)
		    {
			edges.push_back(Edge(EDGE_USED, dmmultipath_node.id(), "device:" + i2->device));
		    }

		    deque<DmmultipathInfo> partitions;
		    s->getDmmultipathInfo(i1->name, partitions);
		    for (deque<DmmultipathInfo>::const_iterator i2 = partitions.begin(); i2 != partitions.end(); ++i2)
		    {
			if (i2->p.p.partitionType == EXTENDED)
			    continue;

			Node partition_node(NODE_PARTITION, i2->p.v.device, i2->p.v.name, i2->p.v.sizeK);
			nodes.push_back(partition_node);

			edges.push_back(Edge(EDGE_SUBDEVICE, dmmultipath_node.id(), partition_node.id()));

			for (list<UsedByInfo>::const_iterator i3 = i2->p.v.usedBy.begin(); i3 != i2->p.v.usedBy.end(); ++i3)
			{
			    edges.push_back(Edge(EDGE_USED, partition_node.id(), "device:" + i3->device));
			}

			if (!i2->p.v.mount.empty())
			{
			    Node mountpoint_node(NODE_MOUNTPOINT, i2->p.v.device, i2->p.v.mount, i2->p.v.sizeK);
			    nodes.push_back(mountpoint_node);

			    edges.push_back(Edge(EDGE_MOUNT, partition_node.id(), mountpoint_node.id()));
			}
		    }

		} break;

		case LOOP:
		case NFSC:
		case CUNKNOWN:
		case COTYPE_LAST_ENTRY:
		    break;
	    }
	}


	ofstream out(filename.c_str());
	classic(out);

	out << "// generated by libstorage version " VERSION << endl;
	out << "// " << hostname() << ", " << datetime() << endl;
	out << endl;

	out << "digraph storage" << endl;
	out << "{" << endl;
	out << "    node [shape=rectangle, style=filled, fontname=\"Arial\"];" << endl;
	out << endl;

	for (list<Node>::const_iterator node = nodes.begin(); node != nodes.end(); ++node)
	    out << "    " << (*node) << endl;

	out << endl;

	for (Ranks::const_iterator rank = ranks.begin(); rank != ranks.end(); ++rank)
	{
	    list<string> ids;
	    for (list<Node>::const_iterator node = nodes.begin(); node != nodes.end(); ++node)
		if (node->type == rank->type)
		    ids.push_back(dotQuote(node->id()));

	    if (!ids.empty())
		out << "    { rank=" << rank->name << "; " << boost::join(ids, " ") << " };" << endl;
	}
	out << endl;

	for (list<Edge>::const_iterator edge = edges.begin(); edge != edges.end(); ++edge)
	    out << "    " << (*edge) << endl;

	out << "}" << endl;

	out.close();

	return !out.bad();
    }

}
