// swift-tools-version:5.9
/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

import PackageDescription

let package = Package(
    name: "coz",
    products: [
        .library(name: "Coz", targets: ["Coz"])
    ],
    targets: [
        // C shim: resolves libcoz's exported symbols with dlsym and mirrors the
        // COZ_INCREMENT_COUNTER logic from include/coz.h.
        .target(
            name: "CCoz",
            cSettings: [
                // glibc only declares RTLD_DEFAULT under _GNU_SOURCE.
                .define("_GNU_SOURCE", .when(platforms: [.linux]))
            ],
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux]))
            ]
        ),
        .target(name: "Coz", dependencies: ["CCoz"]),
        .executableTarget(name: "CozToy", dependencies: ["Coz"]),
        .testTarget(name: "CozTests", dependencies: ["Coz"]),
    ]
)
