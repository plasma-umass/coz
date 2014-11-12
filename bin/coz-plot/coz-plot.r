#!/usr/bin/env Rscript

require(ggplot2)
require(plyr)

args <- commandArgs(trailingOnly = TRUE)

check_valid_data <- function(dat) {
  #print(dat)
  if(length(dat$Location) == 0) {
    print("There are no samples left to plot. You need more runs to collect a usable profile. Make sure your progress points are being executed.")
    q()
  }
}

filename <- 'stdin'
if(length(args) == 1) {
  filename <- args[1]
}

# Read the CSV file
dat <- read.csv(filename, sep=',')

check_valid_data(dat)

# Remove points above 1.0 or below -1.0
dat <- subset(dat, Progress.Speedup < 1.0)
dat <- subset(dat, Progress.Speedup > -1.0)

# Compute the number of points for each line
dat$points <- tabulate(dat$Location)[dat$Location]

# Prune out lines without a complete set of samples
dat <- subset(dat, points >= 10)

check_valid_data(dat)

# Compute the slope of each line's regression line
slopes <- daply(dat, .(Location), function(x) {
  model <- lm(Progress.Speedup~Speedup, data=x, weight=Progress.Count)
  return(coef(model)[2])
})

#maxes <- daply(dat, .(Location), function(x) {
#  q <- quantile(x$Progress.Speedup, probs = c(0.9))
#  return(q[1])
#})

#mins <- daply(dat, .(Location), function(x) {
#  q <- quantile(x$Progress.Speedup, probs = c(0.1))
#  return(q[1])
#})

#dat <- subset(dat, maxes[Location] > 0.3 || mins[Location] < 0.3)

# Save the initial line factor order
l <- levels(dat$Location)

# Reorder the line factor by slope
dat$Location <- factor(dat$Location, levels=l[rev(order(slopes))], ordered=TRUE)

dat <- subset(dat, slopes[dat$Location] > 0.0)

# Graph it
ggplot(dat, aes(x=Speedup, y=Progress.Speedup, color=Location, shape=Progress.Point, weight=Progress.Count, size=Progress.Count)) +
  geom_point(alpha=I(0.5)) +
  facet_wrap(~Location) +
  geom_smooth(method='lm', se=FALSE) +
  theme(legend.position='bottom') +
  scale_y_continuous(limits=c(-1, 1))
