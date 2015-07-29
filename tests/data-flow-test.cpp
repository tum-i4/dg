#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "test-runner.h"
#include "test-dg.h"
#include "../src/analysis/DataFlowAnalysis.h"

#include "../src/DG2Dot.h"

namespace dg {
namespace tests {

class DataFlowA : public analysis::DataFlowAnalysis<TestNode>
{
public:
    DataFlowA(TestDG::BasicBlock *B,
              bool (*ron)(TestNode *), uint32_t fl = 0)
        : analysis::DataFlowAnalysis<TestNode>(B, fl),
          run_on_node(ron) {}

    /* virtual */
    bool runOnNode(TestNode *n)
    {
        return run_on_node(n);
    }
private:
    bool (*run_on_node)(TestNode *);
};

// put it somewhere else
class TestDataFlow : public Test
{
public:
    TestDataFlow() : Test("data flow analysis test")
    {}

    void test()
    {
        run_nums_test();
        run_nums_test_interproc();
    };

    TestDG *create_circular_graph(size_t nodes_num)
    {
        TestDG *d = new TestDG();

        TestDG::BasicBlock *B;
        TestNode **nodes = new TestNode *[nodes_num];

        for (int i = 0; i < nodes_num; ++i) {
            nodes[i] = new TestNode(i);

            d->addNode(nodes[i]);

            B = new TestDG::BasicBlock(nodes[i], nodes[i]);
        }

        // connect to circular graph
        for (int i = 0; i < nodes_num; ++i) {
            TestDG::BasicBlock *B1 = nodes[i]->getBasicBlock();
            TestDG::BasicBlock *B2 = nodes[(i + 1) % nodes_num]->getBasicBlock();
            B1->addSuccessor(B2);
        }

        // graph is circular, it doesn't matter what BB will
        // be entry
        d->setEntryBB(B);
        d->setEntry(B->getFirstNode());

        // add some parameters
        DGParameters<int, TestNode> *params = new DGParameters<int, TestNode>();

        TestNode *ni = new TestNode(nodes_num + 1);
        TestNode *no = new TestNode(nodes_num + 1);
        params->add(nodes_num + 1, ni, no);

        ni = new TestNode(nodes_num + 2);
        no = new TestNode(nodes_num + 2);
        params->add(nodes_num + 2, ni, no);

        d->getEntry()->addParameters(params);

        return d;
    }

    // if for each node we return that nothing
    // changed, we should go through every node
    // just once
    static bool no_change(TestNode *n)
    {
        ++n->counter;
        return false;
    }

    // change first time, no change second time
    static bool one_change(TestNode *n)
    {
        ++n->counter;
        if (n->counter == 1)
            return true;
        else
            return false;
    }

    void run_nums_test()
    {
        #define NODES_NUM 10
        TestDG *d = create_circular_graph(NODES_NUM);
        // B is pointer to the last node, but since the graph is a circle,
        // it doesn't matter what BB we'll use
        DataFlowA dfa(d->getEntryBB(), no_change);
        dfa.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            check(d->getNode(i)->counter == 1,
                  "did not go through the node only one time but %d",
                  d->getNode(i)->counter);

            // zero out the counter for next dataflow run
            d->getNode(i)->counter = 0;
        }

        DataFlowA dfa2(d->getEntryBB(), one_change);
        dfa2.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            check(d->getNode(i)->counter == 2,
                  "did not go through the node only one time but %d",
                  d->getNode(i)->counter);
        }
        #undef NODES_NUM
    }

    void run_nums_test_interproc()
    {
        #define NODES_NUM 5
        TestDG *d = create_circular_graph(NODES_NUM);

        TestNode *last;
        for (auto It : *d) {
            TestDG *sub = create_circular_graph(NODES_NUM);
            It.second->addSubgraph(sub);
        }

        // B is pointer to the last node, but since the graph is a circle,
        // it doesn't matter what BB we'll use
        DataFlowA dfa(d->getEntryBB(), no_change);
        dfa.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            TestNode *n = d->getNode(i);
            check(n->counter == 1,
                  "did not go through the node only one time but %d", n->counter);

            // check that subgraphs are untouched by the dataflow
            // analysis
            for (auto sub : n->getSubgraphs()) {
                // iterate over nodes
                for (auto It : *sub) {
                    TestNode *n = It.second;
                    check(n->counter == 0,
                          "intrAproc. dataflow went to procedures (%d - %d)",
                          n->getKey(), n->counter);

                    TestDG::BasicBlock *BB = n->getBasicBlock();
                    assert(BB);

                    check(BB->getDFSOrder() == 0, "DataFlow went into subgraph blocks");
                }
            }

            // zero out the counter for next dataflow run
            n->counter = 0;
        }

        DataFlowA dfa2(d->getEntryBB(), one_change,
                       analysis::DATAFLOW_INTERPROCEDURAL | analysis::DATAFLOW_BB_NO_CALLSITES);
        dfa2.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            TestNode *n = d->getNode(i);
            check(n->counter == 2,
                  "did not go through the node only one time but %d", n->counter);

            // check that subgraphs are untouched by the dataflow
            // analysis
            for (auto sub : n->getSubgraphs()) {
                // iterate over nodes
                for (auto It : *sub) {
                    TestNode *n = It.second;
                    check(n->counter == 2,
                          "intErproc. dataflow did NOT went to procedures (%d - %d)",
                          n->getKey(), n->counter);

                    TestDG::BasicBlock *BB = n->getBasicBlock();
                    assert(BB);

                    check(BB->getDFSOrder() != 0,
                         "intErproc DataFlow did NOT went into subgraph blocks");

                    n->counter = 0;
                }
            }

            // zero out the counter for next dataflow run
            n->counter = 0;
        }

        // BBlocks now keep call-sites information, so now
        // this should work too
        DataFlowA dfa3(d->getEntryBB(), one_change,
                       analysis::DATAFLOW_INTERPROCEDURAL);
        dfa3.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            TestNode *n = d->getNode(i);
            check(n->counter == 2,
                  "did not go through the node only one time but %d", n->counter);

            // check that subgraphs are untouched by the dataflow
            // analysis
            for (auto sub : n->getSubgraphs()) {
                // iterate over nodes
                for (auto It : *sub) {
                    TestNode *n = It.second;
                    check(n->counter == 2,
                          "intErproc. dataflow did NOT went to procedures (%d - %d)",
                          n->getKey(), n->counter);

                    TestDG::BasicBlock *BB = n->getBasicBlock();
                    assert(BB);

                    check(BB->getDFSOrder() != 0,
                         "intErproc DataFlow did NOT went into subgraph blocks");

                    n->counter = 0;
                }
            }

            // zero out the counter for next dataflow run
            n->counter = 0;
        }

        debug::DG2Dot<int, TestNode> dump(d, debug::PRINT_CFG | debug::PRINT_CALL);
        dump.dump("test.dot", d->getEntryBB());

        #undef NODES_NUM
    }
};

}; // namespace tests
}; // namespace dg

int main(int argc, char *argv[])
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new TestDataFlow());

    return Runner();
}