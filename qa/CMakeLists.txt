add_executable(commitmessagepolicy_test
    commitmessagepolicy_test.cpp
    ../src/commitmessageaiservice.cpp
    ../src/commitmessageaiservice.h
    ../src/commitmessagepolicy.h
)
target_include_directories(commitmessagepolicy_test PRIVATE ../src)
target_link_libraries(commitmessagepolicy_test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME commitmessagepolicy COMMAND commitmessagepolicy_test)

add_executable(repositoryworkflow_test
    repositoryworkflow_test.cpp
    ../src/repositoryworkflow.h
    ../src/batchpushworkflow.cpp
    ../src/batchpushworkflow.h
    ../src/revisionworkflow.cpp
    ../src/revisionworkflow.h
    ../src/aicommitworkflow.cpp
    ../src/aicommitworkflow.h
    ../src/hookworkflow.cpp
    ../src/hookworkflow.h
    ../src/reviewpushworkflow.cpp
    ../src/reviewpushworkflow.h
    ../src/workingtreeworkflow.cpp
    ../src/workingtreeworkflow.h
    ../src/synchronizationworkflow.cpp
    ../src/synchronizationworkflow.h
    ../src/commitmessageaiservice.cpp
    ../src/commitmessageaiservice.h
    ../src/gerritservice.cpp
    ../src/gerritservice.h
    ../src/gitservice.cpp
    ../src/gitservice.h
    ../src/taskreporter.h
    ../src/processrunner.cpp
    ../src/processrunner.h
)
target_include_directories(repositoryworkflow_test PRIVATE ../src)
target_link_libraries(repositoryworkflow_test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME repositoryworkflow COMMAND repositoryworkflow_test)
