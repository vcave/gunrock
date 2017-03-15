//----------------------------------------------------------------------------
// Gunrock -- Fast and Efficient GPU Graph Library
// ----------------------------------------------------------------------------
// This source code is distributed under the terms of LICENSE.TXT
// in the root directory of this source distribution.
// ----------------------------------------------------------------------------

/**
 * @file
 * csc.cuh
 *
 * @brief CSC (Compressed Sparse Column) Graph Data Structure
 */

#pragma once

#include <gunrock/util/array_utils.cuh>
#include <gunrock/graph/graph_base.cuh>
#include <gunrock/graph/coo.cuh>
#include <gunrock/util/binary_search.cuh>

namespace gunrock {
namespace graph {

/**
 * @brief CSC data structure which uses Compressed Sparse Column
 * format to store a graph. It is a compressed way to present
 * the graph as a sparse matrix.
 *
 * @tparam VertexT Vertex identifier type.
 * @tparam SizeT Graph size type.
 * @tparam ValueT Associated value type.
 */
template<
    typename VertexT = int,
    typename SizeT   = VertexT,
    typename ValueT  = VertexT,
    GraphFlag FLAG   = GRAPH_NONE,
    unsigned int cudaHostRegisterFlag = cudaHostRegisterDefault>
struct Csc :
    public GraphBase<VertexT, SizeT, ValueT, FLAG, cudaHostRegisterFlag>
{
    typedef GraphBase<VertexT, SizeT, ValueT, FLAG, cudaHostRegisterFlag> BaseGraph;
    typedef Csc<VertexT, SizeT, ValueT, FLAG, cudaHostRegisterFlag> CscT;

    // Column indices corresponding to all the
    // non-zero values in the sparse matrix
    util::Array1D<SizeT, VertexT,
        util::If_Val<FLAG & GRAPH_PINNED, util::PINNED, util::ARRAY_NONE>::Value,
        cudaHostRegisterFlag> row_indices;

    // List of indices where each row of the
    // sparse matrix starts
    util::Array1D<SizeT, SizeT,
        util::If_Val<FLAG & GRAPH_PINNED, util::PINNED, util::ARRAY_NONE>::Value,
        cudaHostRegisterFlag> column_offsets;

    typedef util::Array1D<SizeT, ValueT,
        util::If_Val<FLAG & GRAPH_PINNED, util::PINNED, util::ARRAY_NONE>::Value,
        cudaHostRegisterFlag> Array_ValueT;

    // List of values attached to edges in the graph
    typename util::If<FLAG & HAS_EDGE_VALUES,
        Array_ValueT, util::NullArray<SizeT, ValueT, FLAG, cudaHostRegisterFlag> >::Type edge_values;

    // List of values attached to nodes in the graph
    typename util::If<FLAG & HAS_NODE_VALUES,
        Array_ValueT, util::NullArray<SizeT, ValueT, FLAG, cudaHostRegisterFlag> >::Type node_values;

    /**
     * @brief CSC Constructor
     *
     * @param[in] pinned Use pinned memory for CSC data structure
     * (default: do not use pinned memory)
     */
    Csc() : BaseGraph()
    {
        column_offsets.SetName("column_offsets");
        row_indices   .SetName("row_indices");
        edge_values   .SetName("edge_values");
        node_values   .SetName("node_values");
    }

    /**
     * @brief CSC destructor
     */
    ~Csc()
    {
        //Release();
    }

    /**
     * @brief Deallocates CSC graph
     */
    cudaError_t Release()
    {
        cudaError_t retval = cudaSuccess;
        if (retval = column_offsets.Release()) return retval;
        if (retval = row_indices   .Release()) return retval;
        if (retval = node_values   .Release()) return retval;
        if (retval = edge_values   .Release()) return retval;
        if (retval = BaseGraph    ::Release()) return retval;
        return retval;
    }

    /**
     * @brief Allocate memory for CSC graph.
     *
     * @param[in] nodes Number of nodes in COO-format graph
     * @param[in] edges Number of edges in COO-format graph
     */
    cudaError_t Allocate(SizeT nodes, SizeT edges,
        util::Location target = GRAPH_DEFAULT_TARGET)
    {
        cudaError_t retval = cudaSuccess;
        if (retval = BaseGraph    ::Allocate(nodes, edges, target))
            return retval;
        if (retval = column_offsets .Allocate(nodes + 1  , target))
            return retval;
        if (retval = row_indices   .Allocate(edges      , target))
            return retval;
        if (retval = node_values   .Allocate(nodes      , target))
            return retval;
        if (retval = edge_values   .Allocate(edges      , target))
            return retval;
        return retval;
    }

    template <
        typename VertexT_in, typename SizeT_in,
        typename ValueT_in, GraphFlag FLAG_in,
        unsigned int cudaHostRegisterFlag_in>
    cudaError_t FromCsc(
        Csc<VertexT_in, SizeT_in, ValueT_in, FLAG_in,
            cudaHostRegisterFlag_in> &source,
        util::Location target = util::LOCATION_DEFAULT,
        cudaStream_t stream = 0)
    {
        cudaError_t retval = cudaSuccess;
        if (target == util::LOCATION_DEFAULT)
            target = source.column_offsets.GetSetted() | source.column_offsets.GetAllocated();

        //if (retval = BaseGraph::Set(source))
        //    return retval;
        this -> nodes = source.CscT::nodes;
        this -> edges = source.CscT::edges;
        this -> directed = source.CscT::directed;

        if (retval = Allocate(source.nodes, source.edges, target))
            return retval;

        if (retval = column_offsets   .Set(source.column_offsets,
            this -> nodes + 1, target, stream))
            return retval;

        if (retval = row_indices.Set(source.row_indices,
            this -> edges, target, stream))
            return retval;

        if (retval = edge_values   .Set(source.edge_values,
            this -> edges, target, stream))
            return retval;

        if (retval = node_values   .Set(source.node_values,
            this -> nodes, target, stream))
            return retval;

        return retval;
    }

    /**
     * @brief Build CSC graph from COO graph, sorted or unsorted
     *
     * @param[in] output_file Output file to dump the graph topology info
     * @param[in] coo Pointer to COO-format graph
     * @param[in] coo_nodes Number of nodes in COO-format graph
     * @param[in] coo_edges Number of edges in COO-format graph
     * @param[in] ordered_rows Are the rows sorted? If not, sort them.
     * @param[in] undirected Is the graph directed or not?
     * @param[in] reversed Is the graph reversed or not?
     * @param[in] quiet Don't print out anything.
     *
     * Default: Assume rows are not sorted.
     */
    template <typename CooT>
    cudaError_t FromCoo(
        CooT &source,
        util::Location target = util::LOCATION_DEFAULT,
        cudaStream_t stream = 0,
        //bool  ordered_rows = false,
        //bool  undirected = false,
        //bool  reversed = false,
        bool  quiet = false)
    {
        //typedef Coo<VertexT_in, SizeT_in, ValueT_in, FLAG_in,
        //    cudaHostRegisterFlag_in> CooT;
        if (!quiet)
        {
            util::PrintMsg("  Converting " +
                std::to_string(source.CooT::nodes) +
                " vertices, " + std::to_string(source.CooT::edges) +
                (source.CooT::directed ? " directed" : " undirected") +
                " edges (" + (source.CooT::edge_order == BY_COLUMN_ASCENDING ? " ordered" : "unordered") +
                " tuples) to CSC format...");
        }

        time_t mark1 = time(NULL);
        cudaError_t retval = cudaSuccess;
        if (target == util::LOCATION_DEFAULT)
            target = source.CooT::edge_pairs.GetSetted() | source.CooT::edge_pairs.GetAllocated();

        /*if (retval = BaseGraph:: template Set<typename CooT::CooT>((typename CooT::CooT)source))
            return retval;
        */
        this -> nodes = source.CooT::nodes;
        this -> edges = source.CooT::edges;
        this -> directed = source.CooT::directed;

        if (retval = Allocate(source.CooT::nodes, source.CooT::edges, target))
            return retval;

        // Sort COO by row
        if (retval = source.CooT::Order(BY_COLUMN_ASCENDING, target, stream))
            return retval;
        //source.CooT::Display();

        // assign row_indices
        if (retval = row_indices.ForEach(source.CooT::edge_pairs,
                []__host__ __device__ (VertexT &row_index,
                const typename CooT::EdgePairT &edge_pair){
                row_index = edge_pair.x;},
                this -> edges, target, stream))
            return retval;

        // assign edge_values
        if (FLAG & HAS_EDGE_VALUES)
            if (retval = edge_values.ForEach(source.CooT::edge_values,
                []__host__ __device__ (ValueT &edge_value,
                const typename CooT::ValueT &edge_value_in){
                edge_value = edge_value_in;},
                this -> edges, target, stream))
            return retval;

        // assign column_offsets
        SizeT edges = this -> edges;
        SizeT nodes = this -> nodes;
        auto column_edge_compare = [] __host__ __device__ (
            const typename CooT::EdgePairT &edge_pair,
            const VertexT &column){
            return edge_pair.y < column;
        };
        if (retval = column_offsets.ForAll(source.CooT::edge_pairs,
            [nodes, edges, column_edge_compare] __host__ __device__ (
                SizeT *column_offsets,
                const typename CooT::EdgePairT *edge_pairs,
                const VertexT &column){
                    if (column < nodes)
                        column_offsets[column] = util::BinarySearch(column,
                            edge_pairs, 0, edges,
                            column_edge_compare);
                    else column_offsets[column] = edges;
                }, this -> nodes + 1, target, stream))
            return retval;

        time_t mark2 = time(NULL);
        if (!quiet)
        {
            util::PrintMsg("Done converting (" +
                std::to_string(mark2 - mark1) + "s).");
        }
        return retval;
    }

    template <typename CsrT>
    cudaError_t FromCsr(
        CsrT &source,
        util::Location target = util::LOCATION_DEFAULT,
        cudaStream_t stream = 0)
    {
        cudaError_t retval = cudaSuccess;
        typedef Coo<VertexT, SizeT, ValueT, FLAG | HAS_COO, cudaHostRegisterFlag> CooT;

        CooT coo;
        retval = coo.FromCsr(source, target, stream);
        if (retval) return retval;
        retval = FromCoo(coo, target, stream);
        if (retval) return retval;
        retval = coo.Release();
        if (retval) return retval;
        return retval;
    }

    /**
     * @brief Display CSC graph to console
     *
     * @param[in] with_edge_value Whether display graph with edge values.
     */
    cudaError_t Display(
        std::string graph_prefix = "",
        SizeT nodes_to_show = 40,
        bool  with_edge_values = true)
    {
        cudaError_t retval = cudaSuccess;
        if (nodes_to_show > this -> nodes)
            nodes_to_show = this -> nodes;
        util::PrintMsg(graph_prefix + "Graph containing " +
            std::to_string(this -> nodes) + " vertices, " +
            std::to_string(this -> edges) + " edges, in CSC format."
            + " Neighbor list of first " + std::to_string(nodes_to_show) +
            " nodes :");
        for (SizeT node = 0; node < nodes_to_show; node++)
        {
            std::string str = "";
            for (SizeT edge = column_offsets[node];
                    edge < column_offsets[node + 1];
                    edge++)
            {
                if (edge - column_offsets[node] > 40) break;
                str = str + "[" + std::to_string(row_indices[edge]);
                if (with_edge_values && (FLAG & HAS_EDGE_VALUES))
                {
                    str = str + "," + std::to_string(edge_values[edge]);
                }
                if (edge - column_offsets[node] != 40 &&
                    edge != column_offsets[node+1] -1)
                    str = str + "], ";
                else str = str + "]";
            }
            if (column_offsets[node + 1] - column_offsets[node] > 40)
                str = str + "...";
            str = str + " : v " + std::to_string(node) +
             " " + std::to_string(column_offsets[node]);
            util::PrintMsg(str);
        }
        return retval;
    }
}; // CSC

} // namespace graph
} // namespace gunrock

// Leave this at the end of the file
// Local Variables:
// mode:c++
// c-file-style: "NVIDIA"
// End:
