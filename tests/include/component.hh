#ifndef COMPONENT_HH
#define COMPONENT_HH

#include "smart_data.hh"
#include "transducer.hh"

template<typename Communicator, SmartUnit Unit>
class Publisher {
    using Transd = Transducer<Unit>;
    using SmD = SmartData<Communicator, Transd >;
public:
    Publisher(Communicator *communicator) {
        Transd _transd(0, 300000);
        _smd = SmD(communicator, _transd);
    }

    ~Publisher() {}
private:
    Transd _transd;
    SmD _smd;
};


template<typename Communicator, typename ... Condition>
class Controller {
public:
    Controller(Communicator *communicator) {
        (smart_data_list.push_back(new SmartData<Communicator, Condition>(communicator)), ...);
        start_data_proc();
    }
    ~Controller() {
        turn_off();
        for (auto* smd : smart_data_list) {
            delete smd;
        }
    }
    void turn_off() {
        _running = false;
        if (_proc_thread.joinable()) {
            _proc_thread.join();
        }
    }
private:
    void start_data_proc() {
        _running = true;
        _proc_thread = std::thread([this]() {
            while (_running) {
                std::vector<unsigned char> data(1474);
                for (auto* smd : smart_data_list) {
                    smd->receive(data.data());
                    // TODO Realizar alguma verificação sobre os dados obtidos?
                }
                process_data();
            }
        });
    }
    void process_data() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
private:
    bool _running;
    std::thread _proc_thread;
    std::vector<SmartData<Communicator, Condition>*> smart_data_list;
};

#endif