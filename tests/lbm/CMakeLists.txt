#############################################################################################################################
#
# Tests for lbm module
#
#############################################################################################################################

waLBerla_link_files_to_builddir( "*.prm" )

waLBerla_compile_test( FILES SweepEquivalenceTest.cpp DEPENDS blockforest timeloop )
waLBerla_execute_test( NAME SweepEquivalenceTest )

waLBerla_compile_test( FILES BoundaryHandlingCommunication.cpp DEPENDS blockforest timeloop )
waLBerla_execute_test( NAME BoundaryHandlingCommunication PROCESSES 8 )

waLBerla_compile_test( FILES UnrollTest.cpp  )


waLBerla_compile_test( FILES boundary/SimplePABTest.cpp DEPENDS field blockforest timeloop vtk )


waLBerla_compile_test( FILES boundary/SimpleDiffusionDirichlet.cpp DEPENDS field blockforest geometry timeloop vtk )
waLBerla_execute_test( NAME SimpleDiffusionDirichletDef COMMAND $<TARGET_FILE:SimpleDiffusionDirichlet>  -l 4 -w 2 -o 1.2 -d 3 -t 333 -c 0 -r 0 )
waLBerla_execute_test( NAME SimpleDiffusionDirichletRef COMMAND $<TARGET_FILE:SimpleDiffusionDirichlet>  -l 4 -w 4 -o 1.2 -d 3 -t 3   -c 0 -r 1 )
waLBerla_execute_test( NAME SimpleDiffusionDirichletCav COMMAND $<TARGET_FILE:SimpleDiffusionDirichlet>  -l 5 -w 3 -o 1.2 -d 3 -t 333 -c 1 )


waLBerla_compile_test( FILES boundary/DiffusionDirichlet.cpp DEPENDS field blockforest geometry timeloop vtk )
waLBerla_execute_test( NAME DiffusionDirichletTest1 COMMAND $<TARGET_FILE:DiffusionDirichlet>  -l 16 -w 1 -o 1.2  -v 0.1  -e 0.097711 -t  50 )
waLBerla_execute_test( NAME DiffusionDirichletTest2 COMMAND $<TARGET_FILE:DiffusionDirichlet>  -l 16 -w 1 -o 1.79 -v 0.05 -e 0.295170 -t 100 )

waLBerla_compile_test( FILES Poiseuille.cpp DEPENDS blockforest gather timeloop vtk gui )


waLBerla_compile_test( FILES DiffusionTest.cpp DEPENDS field blockforest timeloop vtk gui postprocessing)
waLBerla_execute_test( NAME  DiffusionTestCorr COMMAND $<TARGET_FILE:DiffusionTest>  -c 1  -dx 0.01  -dt 0.002  -d 0.005  -v 5  -err 0.0025 )
waLBerla_execute_test( NAME  DiffusionTestNone COMMAND $<TARGET_FILE:DiffusionTest>  -c 0  -dx 0.01  -dt 0.002  -d 0.005  -v 5  -err 0.0040 )


waLBerla_compile_test( FILES refinement/Uniformity.cpp DEPENDS blockforest stencil )
waLBerla_execute_test( NAME UniformityShortTest COMMAND $<TARGET_FILE:Uniformity> --shortrun PROCESSES 4                )
waLBerla_execute_test( NAME UniformityLongTest  COMMAND $<TARGET_FILE:Uniformity>            PROCESSES 4 LABELS longrun verylongrun CONFIGURATIONS Release RelWithDbgInfo )


waLBerla_compile_test( FILES refinement/CommunicationEquivalence.cpp DEPENDS blockforest stencil )
waLBerla_execute_test( NAME CommunicationEquivalenceShortTest COMMAND $<TARGET_FILE:CommunicationEquivalence> --shortrun PROCESSES 4                )
waLBerla_execute_test( NAME CommunicationEquivalenceLongTest  COMMAND $<TARGET_FILE:CommunicationEquivalence>            PROCESSES 4 LABELS longrun CONFIGURATIONS Release RelWithDbgInfo )


waLBerla_compile_test( FILES refinement/NonConstantDiffusion.cpp DEPENDS field blockforest geometry timeloop vtk )


waLBerla_compile_test( FILES geometry/IntersectionRatioTest.cpp DEPENDS geometry )
waLBerla_execute_test( NAME IntersectionRatioTest COMMAND $<TARGET_FILE:IntersectionRatioTest> )

waLBerla_compile_test( FILES evaluations/PermeabilityTest.cpp DEPENDS field blockforest geometry timeloop vtk )
waLBerla_execute_test( NAME PermeabilityTest_TRT_16_4 COMMAND $<TARGET_FILE:PermeabilityTest> --length 16 --kappa 0.85 --omega 0.3 --collisionModel TRT --timesteps 1001 --epsilon 0.005 PROCESSES 4 CONFIGURATIONS Release RelWithDbgInfo )
waLBerla_execute_test( NAME PermeabilityTest_TRT_32_4 COMMAND $<TARGET_FILE:PermeabilityTest> --length 32 --kappa 0.85 --omega 0.3 --collisionModel TRT --timesteps 1001 --epsilon 0.005 PROCESSES 4 CONFIGURATIONS Release RelWithDbgInfo )
waLBerla_execute_test( NAME PermeabilityTest_TRT_64_1 COMMAND $<TARGET_FILE:PermeabilityTest> --length 64 --kappa 0.85 --omega 0.3 --collisionModel TRT --timesteps 1001 --epsilon 0.025 PROCESSES 1 CONFIGURATIONS Release RelWithDbgInfo )
waLBerla_execute_test( NAME PermeabilityTest_TRT_64_4 COMMAND $<TARGET_FILE:PermeabilityTest> --length 64 --kappa 0.85 --omega 0.3 --collisionModel TRT --timesteps 1001 --epsilon 0.025 PROCESSES 4 CONFIGURATIONS Release RelWithDbgInfo )
waLBerla_execute_test( NAME PermeabilityTest_TRT_64_8 COMMAND $<TARGET_FILE:PermeabilityTest> --length 64 --kappa 0.85 --omega 0.3 --collisionModel TRT --timesteps 1001 --epsilon 0.025 PROCESSES 8 CONFIGURATIONS Release RelWithDbgInfo )

waLBerla_compile_test( FILES initializer/PdfFieldInitializerTest.cpp )
waLBerla_execute_test( NAME PdfFieldInitializerTest COMMAND $<TARGET_FILE:PdfFieldInitializerTest> ${CMAKE_CURRENT_SOURCE_DIR}/PdfFieldInitializerTest.prm PROCESSES 4 CONFIGURATIONS Release RelWithDbgInfo )
