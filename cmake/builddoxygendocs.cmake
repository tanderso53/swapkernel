######################################################################
#                                                                    #
#                                                                    #
#                Library Documentation Build Config                  #
#                          For CMake                                 #
#                                                                    #
#                      By Tyler J. Anderson                          #
#                                                                    #
#                                                                    #
######################################################################

# Include this library in the top-level CMakeLists.txt and call the
# macro builddoxygendocs(prjname) to build documentation for the
# project
#
# Will build HTML docs and optionally LaTeX if a LaTeX system was
# found
#
# Dependencies:
# - doxygen
# - doxygen-awesome
#
# Place doxygen awesome in <project root>/doc/doxygen-awesome-css

macro(builddoxygendocs prjname)

  ##########################
  # DOCUMENTATION OVERIDES #
  ##########################

  set(DOXYGEN_OUTPUT_DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/doc)

  set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)

  set(DOXYGEN_GENERATE_TREEVIEW YES)

  set(DOXYGEN_FULL_SIDEBAR YES)

  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE
    ${PROJECT_SOURCE_DIR}/README.md)

  set(DOXYGEN_HTML_EXTRA_STYLESHEET
    ${CMAKE_CURRENT_LIST_DIR}/doc/doxygen-awesome-css/doxygen-awesome.css)

  set(DOXYGEN_EXCLUDE_PATTERNS
    */doxygen-awesome-css/*
    */munit/*
    */lib/*)

  #############################
  # LIBRARY API DOCUMENTATION #
  #############################

  option(${prjname}_BUILD_DOCS
    "Build documentation" OFF)
  option(${prjname}_BUILD_DOCS_PDF
    "enable building of PDF docs" ON)

  if(${prjname}_BUILD_DOCS)
    find_package(Doxygen
      REQUIRED dot
      OPTIONAL_COMPONENTS mscgen dia)
  endif()

  if(${prjname}_BUILD_DOCS AND DOXYGEN_FOUND)

    find_package(LATEX COMPONENTS PDFLATEX MAKEINDEX)

    if(LATEX_FOUND AND ${prjname}_BUILD_DOCS_PDF)

      set(DOXYGEN_GENERATE_LATEX YES)

    else()

      message(NOTICE "PDF Documentation will not be build")

    endif()

    doxygen_add_docs(${PROJECT_NAME}-builddocs ALL
      ${PROJECT_SOURCE_DIR}
      COMMENT "Generate API documentation")

    # Install docs

    install(DIRECTORY ${DOXYGEN_OUTPUT_DIRECTORY}/html/
      DESTINATION ${CMAKE_INSTALL_DOCDIR}/html)

    # If LATEX available, build and install a PDF as well

    if(LATEX_FOUND AND ${prjname}_BUILD_DOCS_PDF)

      add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pdf
	COMMAND make -C ${DOXYGEN_OUTPUT_DIRECTORY}/latex
	LATEX_CMD=${PDFLATEX_COMPILER}
	COMMAND cp ${DOXYGEN_OUTPUT_DIRECTORY}/latex/refman.pdf
	${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pdf
	DEPENDS ${PROJECT_NAME}-builddocs)

      add_custom_target(${PROJECT_NAME}-builddocs-pdf ALL
	DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pdf
	COMMENT "Building PDF Documentation")

      install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pdf
	TYPE DOC)

    endif()

  else()

    message(NOTICE "Documentation will not be build")

  endif()

endmacro()
