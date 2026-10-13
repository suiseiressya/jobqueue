# Cerr race

## Reading the bug

Full bug trace:

```bash
==539933== ---Thread-Announcement------------------------------------------
==539933== 
==539933== Thread #63 was created
==539933==    at 0x590D543: clone (clone.S:76)
==539933==    by 0x590D6A0: __clone_internal_fallback (clone-internal.c:64)
==539933==    by 0x590D6A0: __clone_internal (clone-internal.c:109)
==539933==    by 0x5884BC5: create_thread (pthread_create.c:298)
==539933==    by 0x58858A6: pthread_create@@GLIBC_2.34 (pthread_create.c:858)
==539933==    by 0x4B89271: pthread_create_WRK (hg_intercepts.c:449)
==539933==    by 0x549E961: __gthread_create (gthr-default.h:709)
==539933==    by 0x549E961: std::thread::_M_start_thread(std::unique_ptr<std::thread::_State, std::default_delete<std::thread::_State> >, void (*)()) (thread.cc:173)
==539933==    by 0x40F7A88: std::thread::thread<ThreadPool::Start()::{lambda()#1}, , void>(ThreadPool::Start()::{lambda()#1}&&) (std_thread.h:175)
==539933==    by 0x40F79BA: _ZSt12construct_atISt6threadJZN10ThreadPool5StartEvEUlvE_EQaant20is_unbounded_array_vIT_ErqXgsnwcvPvLi0E_S3_pispcl7declvalIT0_EEEEEPS3_S6_DpOS5_ (stl_construct.h:110)
==539933==    by 0x40F752B: construct<std::thread, ThreadPool::Start()::<lambda()> > (alloc_traits.h:716)
==539933==    by 0x40F752B: std::thread& std::vector<std::thread, std::allocator<std::thread> >::emplace_back<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (vector.tcc:121)
==539933==    by 0x40F73CF: ThreadPool::Start() (thread_pool.cc:66)
==539933==    by 0x403FA7C: CATCH2_INTERNAL_TEST_14() (thread_pool_test.cc:231)
==539933==    by 0x41ABD29: Catch::(anonymous namespace)::TestInvokerAsFunction::invoke() const (catch_test_registry.cpp:60)
==539933== 
==539933== ---Thread-Announcement------------------------------------------
==539933== 
==539933== Thread #62 was created
==539933==    at 0x590D543: clone (clone.S:76)
==539933==    by 0x590D6A0: __clone_internal_fallback (clone-internal.c:64)
==539933==    by 0x590D6A0: __clone_internal (clone-internal.c:109)
==539933==    by 0x5884BC5: create_thread (pthread_create.c:298)
==539933==    by 0x58858A6: pthread_create@@GLIBC_2.34 (pthread_create.c:858)
==539933==    by 0x4B89271: pthread_create_WRK (hg_intercepts.c:449)
==539933==    by 0x549E961: __gthread_create (gthr-default.h:709)
==539933==    by 0x549E961: std::thread::_M_start_thread(std::unique_ptr<std::thread::_State, std::default_delete<std::thread::_State> >, void (*)()) (thread.cc:173)
==539933==    by 0x40F7A88: std::thread::thread<ThreadPool::Start()::{lambda()#1}, , void>(ThreadPool::Start()::{lambda()#1}&&) (std_thread.h:175)
==539933==    by 0x40F79BA: _ZSt12construct_atISt6threadJZN10ThreadPool5StartEvEUlvE_EQaant20is_unbounded_array_vIT_ErqXgsnwcvPvLi0E_S3_pispcl7declvalIT0_EEEEEPS3_S6_DpOS5_ (stl_construct.h:110)
==539933==    by 0x40F769B: construct<std::thread, ThreadPool::Start()::<lambda()> > (alloc_traits.h:716)
==539933==    by 0x40F769B: void std::vector<std::thread, std::allocator<std::thread> >::_M_realloc_append<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (vector.tcc:594)
==539933==    by 0x40F755D: std::thread& std::vector<std::thread, std::allocator<std::thread> >::emplace_back<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (vector.tcc:127)
==539933==    by 0x40F73CF: ThreadPool::Start() (thread_pool.cc:66)
==539933==    by 0x403FA7C: CATCH2_INTERNAL_TEST_14() (thread_pool_test.cc:231)
==539933== 
==539933== ----------------------------------------------------------------
==539933== 
==539933== Possible data race during read of size 8 at 0x42E6AD8 by thread #63
==539933== Locks held: none
==539933==    at 0x550EE8E: width (ios_base.h:790)
==539933==    by 0x550EE8E: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:95)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==    by 0x40F7EA5: std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::operator()() (std_thread.h:310)
==539933==    by 0x40F7E89: std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> > >::_M_run() (std_thread.h:255)
==539933==    by 0x549E858: execute_native_thread_routine (thread.cc:105)
==539933==    by 0x4B8947B: mythread_wrapper (hg_intercepts.c:410)
==539933== 
==539933== This conflicts with a previous write of size 8 by thread #62
==539933== Locks held: none
==539933==    at 0x550EF70: width (ios_base.h:801)
==539933==    by 0x550EF70: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:110)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==  Address 0x42e6ad8 is 24 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"
==539933== 
[service] job ==539933== ----------------------------------------------------------------
==539933== 
==539933== Possible data race during write of size 8 at 0x42E6AD8 by thread #63
==539933== Locks held: none
==539933==    at 0x550EF70: width (ios_base.h:801)
==539933==    by 0x550EF70: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:110)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==    by 0x40F7EA5: std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::operator()() (std_thread.h:310)
==539933==    by 0x40F7E89: std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> > >::_M_run() (std_thread.h:255)
==539933==    by 0x549E858: execute_native_thread_routine (thread.cc:105)
==539933==    by 0x4B8947B: mythread_wrapper (hg_intercepts.c:410)
==539933== 
==539933== This conflicts with a previous write of size 8 by thread #62
==539933== Locks held: none
==539933==    at 0x550EF70: width (ios_base.h:801)
==539933==    by 0x550EF70: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:110)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==  Address 0x42e6ad8 is 24 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"
==539933== 
==539933== ----------------------------------------------------------------
==539933== 
==539933== Possible data race during read of size 4 at 0x42E6AE4 by thread #63
==539933== Locks held: none
==539933==    at 0x550EFD1: _Disable_exceptions (ostream.h:515)
==539933==    by 0x550EFD1: ~sentry (ostream.h:580)
==539933==    by 0x550EFD1: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==    by 0x40F7EA5: std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::operator()() (std_thread.h:310)
==539933==    by 0x40F7E89: std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> > >::_M_run() (std_thread.h:255)
==539933==    by 0x549E858: execute_native_thread_routine (thread.cc:105)
==539933==    by 0x4B8947B: mythread_wrapper (hg_intercepts.c:410)
==539933== 
==539933== This conflicts with a previous write of size 4 by thread #62
==539933== Locks held: none
==539933==    at 0x550EFD5: _Disable_exceptions (ostream.h:516)
==539933==    by 0x550EFD5: ~sentry (ostream.h:580)
==539933==    by 0x550EFD5: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==  Address 0x42e6ae4 is 36 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"
==539933== 
==539933== ----------------------------------------------------------------
==539933== 
==539933== Possible data race during write of size 4 at 0x42E6AE4 by thread #63
==539933== Locks held: none
==539933==    at 0x550EFD5: _Disable_exceptions (ostream.h:516)
==539933==    by 0x550EFD5: ~sentry (ostream.h:580)
==539933==    by 0x550EFD5: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==    by 0x40F7EA5: std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::operator()() (std_thread.h:310)
==539933==    by 0x40F7E89: std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> > >::_M_run() (std_thread.h:255)
==539933==    by 0x549E858: execute_native_thread_routine (thread.cc:105)
==539933==    by 0x4B8947B: mythread_wrapper (hg_intercepts.c:410)
==539933== 
==539933== This conflicts with a previous write of size 4 by thread #62
==539933== Locks held: none
==539933==    at 0x550EFD5: _Disable_exceptions (ostream.h:516)
==539933==    by 0x550EFD5: ~sentry (ostream.h:580)
==539933==    by 0x550EFD5: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==  Address 0x42e6ae4 is 36 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"
==539933== 
4c1b585e-8c8e-48bf-b759-e1ce236f08b1: PENDING -> RUNNING skipped, row no longer PENDING in DB 
[service] job 7c6d1e7b-b0ae-4936-89f2-836b183dbb24: PENDING -> RUNNING skipped, row no longer PENDING in DB 
[service] job b87756e5-147e-42b2-89b7-882443dbdfe3==539933== ---Thread-Announcement------------------------------------------
==539933== 
==539933== Thread #60 was created
==539933==    at 0x590D543: clone (clone.S:76)
==539933==    by 0x590D6A0: __clone_internal_fallback (clone-internal.c:64)
==539933==    by 0x590D6A0: __clone_internal (clone-internal.c:109)
==539933==    by 0x5884BC5: create_thread (pthread_create.c:298)
==539933==    by 0x58858A6: pthread_create@@GLIBC_2.34 (pthread_create.c:858)
==539933==    by 0x4B89271: pthread_create_WRK (hg_intercepts.c:449)
==539933==    by 0x549E961: __gthread_create (gthr-default.h:709)
==539933==    by 0x549E961: std::thread::_M_start_thread(std::unique_ptr<std::thread::_State, std::default_delete<std::thread::_State> >, void (*)()) (thread.cc:173)
==539933==    by 0x40F7A88: std::thread::thread<ThreadPool::Start()::{lambda()#1}, , void>(ThreadPool::Start()::{lambda()#1}&&) (std_thread.h:175)
==539933==    by 0x40F79BA: _ZSt12construct_atISt6threadJZN10ThreadPool5StartEvEUlvE_EQaant20is_unbounded_array_vIT_ErqXgsnwcvPvLi0E_S3_pispcl7declvalIT0_EEEEEPS3_S6_DpOS5_ (stl_construct.h:110)
==539933==    by 0x40F769B: construct<std::thread, ThreadPool::Start()::<lambda()> > (alloc_traits.h:716)
==539933==    by 0x40F769B: void std::vector<std::thread, std::allocator<std::thread> >::_M_realloc_append<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (vector.tcc:594)
==539933==    by 0x40F755D: std::thread& std::vector<std::thread, std::allocator<std::thread> >::emplace_back<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (vector.tcc:127)
==539933==    by 0x40F73CF: ThreadPool::Start() (thread_pool.cc:66)
==539933==    by 0x403FA7C: CATCH2_INTERNAL_TEST_14() (thread_pool_test.cc:231)
==539933== 
==539933== ----------------------------------------------------------------
==539933== 
==539933== Possible data race during write of size 8 at 0x42E6AD8 by thread #60
==539933== Locks held: none
==539933==    at 0x550EF70: width (ios_base.h:801)
==539933==    by 0x550EF70: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:110)
==539933==    by 0x40E6EB2: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==    by 0x40F7EA5: std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::operator()() (std_thread.h:310)
==539933==    by 0x40F7E89: std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> > >::_M_run() (std_thread.h:255)
==539933==    by 0x549E858: execute_native_thread_routine (thread.cc:105)
==539933==    by 0x4B8947B: mythread_wrapper (hg_intercepts.c:410)
==539933==    by 0x5885041: start_thread (pthread_create.c:454)
==539933== 
==539933== This conflicts with a previous write of size 8 by thread #63
==539933== Locks held: none
==539933==    at 0x550EF70: width (ios_base.h:801)
==539933==    by 0x550EF70: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:110)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==  Address 0x42e6ad8 is 24 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"
==539933== 
==539933== ----------------------------------------------------------------
==539933== 
==539933== Possible data race during read of size 4 at 0x42E6AE4 by thread #60
==539933== Locks held: none
==539933==    at 0x550EFD1: _Disable_exceptions (ostream.h:515)
==539933==    by 0x550EFD1: ~sentry (ostream.h:580)
==539933==    by 0x550EFD1: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x40E6EB2: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==    by 0x40F7EA5: std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::operator()() (std_thread.h:310)
==539933==    by 0x40F7E89: std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> > >::_M_run() (std_thread.h:255)
==539933==    by 0x549E858: execute_native_thread_routine (thread.cc:105)
==539933==    by 0x4B8947B: mythread_wrapper (hg_intercepts.c:410)
==539933==    by 0x5885041: start_thread (pthread_create.c:454)
==539933== 
==539933== This conflicts with a previous write of size 4 by thread #63
==539933== Locks held: none
==539933==    at 0x550EFD5: _Disable_exceptions (ostream.h:516)
==539933==    by 0x550EFD5: ~sentry (ostream.h:580)
==539933==    by 0x550EFD5: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==  Address 0x42e6ae4 is 36 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"
==539933== 
==539933== ----------------------------------------------------------------
==539933== 
==539933== Possible data race during write of size 4 at 0x42E6AE4 by thread #60
==539933== Locks held: none
==539933==    at 0x550EFD5: _Disable_exceptions (ostream.h:516)
==539933==    by 0x550EFD5: ~sentry (ostream.h:580)
==539933==    by 0x550EFD5: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x40E6EB2: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==    by 0x40F7EA5: std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::operator()() (std_thread.h:310)
==539933==    by 0x40F7E89: std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> > >::_M_run() (std_thread.h:255)
==539933==    by 0x549E858: execute_native_thread_routine (thread.cc:105)
==539933==    by 0x4B8947B: mythread_wrapper (hg_intercepts.c:410)
==539933==    by 0x5885041: start_thread (pthread_create.c:454)
==539933== 
==539933== This conflicts with a previous write of size 4 by thread #63
==539933== Locks held: none
==539933==    at 0x550EFD5: _Disable_exceptions (ostream.h:516)
==539933==    by 0x550EFD5: ~sentry (ostream.h:580)
==539933==    by 0x550EFD5: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==  Address 0x42e6ae4 is 36 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"
==539933== 
```

## Reasoning

How many threads? 3 threads (#60, #62, #63). At first glance the races between them is very similar so we can just compare two. 

Read the last line of one of the possible data race: `Address 0x42e6ae4 is 36 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"`, this tells us which memory the threads are racing on. `_ZSt4cerr@GLIBCXX_3.4` is `std::cerr`. This is not random knowledge - you can verify this with `c++filt`:

```bash
$ c++filt _ZSt4cerr
std::cerr
```

Then read carefully through a data race section:
```bash
==539933== Possible data race during write of size 4 at 0x42E6AE4 by thread #60
==539933== Locks held: none
==539933==    at 0x550EFD5: _Disable_exceptions (ostream.h:516)
==539933==    by 0x550EFD5: ~sentry (ostream.h:580)
==539933==    by 0x550EFD5: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x40E6EB2: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==    by 0x40F7EA5: std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::operator()() (std_thread.h:310)
==539933==    by 0x40F7E89: std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> > >::_M_run() (std_thread.h:255)
==539933==    by 0x549E858: execute_native_thread_routine (thread.cc:105)
==539933==    by 0x4B8947B: mythread_wrapper (hg_intercepts.c:410)
==539933==    by 0x5885041: start_thread (pthread_create.c:454)
==539933== 
==539933== This conflicts with a previous write of size 4 by thread #63
==539933== Locks held: none
==539933==    at 0x550EFD5: _Disable_exceptions (ostream.h:516)
==539933==    by 0x550EFD5: ~sentry (ostream.h:580)
==539933==    by 0x550EFD5: std::basic_ostream<char, std::char_traits<char> >& std::__ostream_insert<char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*, long) (ostream_insert.h:121)
==539933==    by 0x550F411: std::basic_ostream<char, std::char_traits<char> >& std::operator<< <std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >&, char const*) (ostream.h:739)
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
==539933==    by 0x40F7F53: void std::__invoke_impl<void, ThreadPool::Start()::{lambda()#1}>(std::__invoke_other, ThreadPool::Start()::{lambda()#1}&&) (invoke.h:63)
==539933==    by 0x40F7F16: std::__invoke_result<ThreadPool::Start()::{lambda()#1}>::type std::__invoke<ThreadPool::Start()::{lambda()#1}>(ThreadPool::Start()::{lambda()#1}&&) (invoke.h:98)
==539933==    by 0x40F7ED1: void std::thread::_Invoker<std::tuple<ThreadPool::Start()::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (std_thread.h:303)
==539933==  Address 0x42e6ae4 is 36 bytes inside data symbol "_ZSt4cerr@GLIBCXX_3.4"
==539933== 
```

After some investigating we found out the part that correspond to our code: 
```bash
# Thread #60
==539933==    by 0x40E6EB2: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)

# Thread #63
==539933==    by 0x40E6E8D: JobService::WaitAndPop() (job_service.cc:84)
==539933==    by 0x40F703F: ThreadPool::WorkerLoop() (thread_pool.cc:27)
==539933==    by 0x40F738C: ThreadPool::Start()::{lambda()#1}::operator()() const (thread_pool.cc:66)
```

Both threads point to the same line. Also, `Locks held: none` means that no lock were held.

Diving deep into the code:

```cpp
// job_service.cc 
    std::unique_lock lock(mut_);

    // ...

    lock.unlock();

    size_t affected = job_repo_.UpdateJobStatus(job_id, kPending, kRunning);

    // if affected == 0: row is not PENDING between Pop() and UPDATE 
    // Caused by HTTP DELETE, which call CancelPendingJobById which send another call to DB 
    // DB will determine the race result; indeterministic
    if (affected == 0) {
        std::cerr << "[service] job " << job_id.ToString()
                    << ": PENDING -> RUNNING skipped, row no longer PENDING in DB "
                    << std::endl;
        lock.lock();
        continue;
    }
```

The bug is `std::cerr` here. Think of which scenario this could happen. Most obviously, two threads calling this the same time, causing printing order to be scrambled. It would only call `std::cerr` if the previous DB call fails, and since it is a DB call we unlocked the lock for other threads to access critical section. However, we overlooked the `std::cerr` part by not putting a lock on it. 

Note that this is not about accessing same data since each thread has a separate job_id (as ToString() doesn't show up on the log). This is about std::cerr itself. Per ChatGPT:

"std::cerr touches shared internal state in its std::ostream object and underlying std::streambuf, which may include buffer pointers, formatting state, and output state.

If two threads modify the same non-atomic internal state concurrently without synchronization, that can cause a data race."

## Impact

Does this make the code break? No. At worst it just messes up the log ordering, but still helpful to solve. 

## Fix

Using a mutex for this is way too overkill and it blocks other thread. Use `std::osyncstream`: it buffers the whole line and writes it to `std::cerr` at once when it goes out of scope. Simply replace `std::cerr` with `std::osyncstream(std::cerr)`. 


## Verify

Sometimes the bug doesn't show up. Run Helgrind 3 times. All should record 0 errors. 

```bash
for i in $(seq 3); do make helgrind 2>&1 | grep "ERROR SUMMARY"; done
```


