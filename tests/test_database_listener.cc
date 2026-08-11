#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <pqxx/pqxx>
namespace {
class TestDatabaseListener final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;
    void testCaseStarting(Catch::TestCaseInfo const&) override {
        pqxx::connection connection{
            "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
        pqxx::work transaction{connection};
        transaction.exec("TRUNCATE TABLE jobs");
        transaction.commit();
    }
};
}
CATCH_REGISTER_LISTENER(TestDatabaseListener)
