#include <cyber/jobs/job_system.h>
#include <cyber/jobs/jobs.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::jobs;

CG_TEST(jobs_public_headers)
{
    static_assert(!std::is_copy_constructible_v<JobSystem>);
    static_assert(!std::is_copy_assignable_v<JobSystem>);
    static_assert(std::is_move_constructible_v<JobSystem>);
    static_assert(std::is_move_assignable_v<JobSystem>);

    JobSystem system;
    CG_EXPECT_FALSE(system.IsRunning());
    CG_EXPECT_EQ(system.WorkerCount(), u32{0});
    CG_EXPECT_FALSE(system.IsWorkerThread());
    CG_EXPECT_FALSE(system.Submit(JobDesc{}).IsValid());
    CG_EXPECT_FALSE(system.SubmitDetached(JobDesc{}));

    JobDesc desc{};
    CG_EXPECT_TRUE(desc.name != nullptr);
    CG_EXPECT_TRUE(desc.function == nullptr);
    CG_EXPECT_TRUE(JobSystem::HardwareThreadCount() > 0);
}
