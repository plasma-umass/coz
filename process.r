# Run with r --no-save --args [results filename here] < process.r

require(ggplot2)
require(plyr)

args <- commandArgs(trailingOnly = TRUE)

# Read the CSV file
dat <- read.csv(args[1], sep='\t')

# Compute the slope of each block's regression line
slopes <- daply(dat, .(block), function(x) {
  model <- lm(counter_speedup~block_speedup, data=x)
  return(coef(model)[2])
})

# Save the initial block factor order
l <- levels(dat$block)

# Reorder the block factor by slope
dat$block <- factor(dat$block, levels=l[rev(order(slopes))], ordered=TRUE)

# Compute the number of points for each block
dat$points <- tabulate(dat$block)[dat$block]

# Prune out blocks with a single point
dat <- subset(dat, points > 3)

# Graph it
ggplot(dat, aes(x=block_speedup, y=counter_speedup, color=counter)) +
  geom_point() +
  facet_wrap(~block) +
  geom_smooth(method='lm', se=FALSE) +
  theme(legend.position='bottom') +
  scale_y_continuous(limits=c(-1, 1))