#include "mouse_native/mouse_diagnostics.h"
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
}
int main(int argc,char** argv) {
    using namespace mouse_native;
    try {
        MouseDiagnosticOptions options;
        options.directory=argc>1 ? argv[1] : "runs/mouse_diagnostic_tests";
        options.capacity=1024; options.segment_bytes=5000;
        MouseDiagnostics normal(options,"{\"schema\":1,\"fixture\":true}");
        for(int i=1;i<=100;++i) {
            MouseDiagnosticRecord r; r.clock.tick=i; r.source={i,-i}; r.final=r.source;
            r.window.submitted_counts=r.final; r.window.committed=true;
            r.delivered=true;
            r.point_tolerance_px=3;r.point_inside[0]=true;r.point_inside[1]=false;
            r.source_aim[0]=321;r.source_aim[1]=256;
            r.position_u[0]=.025f;r.motion_u[0]=-.2f;r.effective_motion_u[0]=-.0125f;
            r.selector_generation=9;
            check(normal.enqueue(r),"normal queue acceptance");
        }
        normal.stop(true);
        const auto n=normal.counters();
        check(n.accepted==100 && n.written==100 && n.dropped==0 && !n.failed,"flush all accepted records");
        std::uint64_t last=0; int files=0;
        for(const auto& path:std::filesystem::directory_iterator(normal.directory())) {
            if(path.path().extension()!=".jsonl") continue;
            ++files; std::ifstream in(path.path()); std::string line;
            while(std::getline(in,line)) {
                const auto begin=line.find("\"tick\":")+7;
                const auto id=std::stoull(line.substr(begin));
                check(id>=1 && id<=100,"serialized tick identity"); ++last;
                check(line.find("\"submitted\":["+std::to_string(id)+",-"+std::to_string(id)+"]")!=std::string::npos,
                    "submitted physical accounting survives serialization");
            }
        }
        check(last==100 && files>1,"segment rotation keeps every record");
        options.capacity=2; options.start_writer=false;
        MouseDiagnostics overflow(options,"{}");
        check(overflow.enqueue({}) && overflow.enqueue({}) && !overflow.enqueue({}),"bounded queue overflow");
        overflow.stop();
        check(overflow.counters().dropped==1 && overflow.counters().discarded==2,"explicit loss accounting");
        options.start_writer=true; options.max_bytes=1;
        MouseDiagnostics budget(options,"{}"); budget.enqueue({}); budget.stop();
        check(budget.counters().budget_exhausted && budget.counters().discarded==1,"budget stops without deleting old logs");
        options.enabled=false; MouseDiagnostics disabled(options,"{}");
        check(!disabled.enqueue({}) && disabled.directory().empty(),"disabled logger creates no session");
        std::cout<<"PASS logger ordering, rotation, flush, overflow and byte budget; fixture="<<normal.directory()<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
