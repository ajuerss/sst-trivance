#include <vector>
#include <assert.h>
class SendMachineRRArbiter
{
public:
    SendMachineRRArbiter(int numSendMachines)
        : m_numSendMachines(numSendMachines), m_rr_index(0), m_tot_ready_pkts(0)
    {
        m_ready_pkts.resize(numSendMachines, 0);
    }

    void signalAvailability(int id)
    {
        m_ready_pkts[id]++;
        m_tot_ready_pkts++;
    }

    int getAvailabilityCount()
    {
        return m_tot_ready_pkts;
    }

    bool isEmpty() { return m_tot_ready_pkts == 0; }

    int getNextIdx()
    {
        if (m_ready_pkts[m_rr_index] == 0) moveRRIdx();
        assert(m_ready_pkts[m_rr_index] > 0);
        return m_rr_index;
    }

    int popNextIdx()
    {
        int res = 0;
        assert(m_ready_pkts[m_rr_index] > 0);
        m_ready_pkts[m_rr_index]--;
        res = m_ready_pkts[m_rr_index];
        m_tot_ready_pkts--;
        moveRRIdx();
        return res;
    }

    void printStatus(uint32_t node_id)
    {
        printf("[%d] tot_ready_pkts: %d; m_rr_index: %d; m_numSendMachines: %d; ", node_id, m_tot_ready_pkts, m_rr_index, m_numSendMachines);
        for (int i=0; i<m_numSendMachines; i++)
        {
            printf("%d ", m_ready_pkts[i]);
        }
        printf("\n");
    }

private:
    void moveRRIdx() {
        for (int i = 0; i < m_numSendMachines; i++)
        {
            m_rr_index = (m_rr_index + 1) % m_numSendMachines;
            if (m_ready_pkts[m_rr_index] > 0) return;
        }
    }

private:
    int m_numSendMachines;
    int m_rr_index;
    int m_tot_ready_pkts;
    std::vector<int> m_ready_pkts;
};