# Code Reviews

Code reviews require a remote branch, like:

```shell
git checkout -b <branch>    # create branch
git push origin -u HEAD     # push to origin
```

Use your source-hosting service's review workflow to open a review from that
branch. What follows are Panthera-specific policies and tips.


## Policies

* If reviewers should **not** look at the review yet, or it's still in-progress.
  Mark as a Draft. Ideally, reviewers should not be added unless you have
  questions for them.
* Assignee can be anyone, usually it's the author, or the person that takes
  "ownership" of the MR.
* It is generally a good idea to only assign one "primary" reviewer because it
  avoids multiple reviewers each thinking the other will perform the review.
  "Secondary" reviewers can be mentioned in a comment, and they can leave feedback
  if they have any.
* However, if you have _multiple_ reviewers, please be clear what each one
  should look at.

## Goals and Tasks

### Author

* For large/complex changes, consider a "design review/discussion" ahead-of-time
  to minimize discussion in the MR, which could be:
    * A meeting with certain developers (usually those who'd be reviewers)
    * A topic at the "Panthera Modeling" meeting for widespread changes/features
* Aim to minimize the size/scope of the MR, this may include either:
    * Splitting out _independent_ changes into separate MRs
    * Separating changes by commit within the same MR
* Describe what the review is adding/changing
* Indicate if there's any performance drift (and on what configs), please use the following format:
  ```
  Expected Perf Drift: <value or negligible>
  ```
  Where "value" is a relative speedup vs. the baseline, some examples:
    * "-0.1%" (assumed to be current project)
    * "+1.2% on Rogue"
    * "Negligible" (usually used for <0.1%), or "None"
    * "None (disabled)"
    * "Neglibible (all configs)"

### Reviewer

* Make sure the code is clear, consistent, concise, and maintainable
    * Identify if any code/capabilities already exist elsewhere
* Verify behavior looks correct
    * Or if tests are available, do the outputs make sense?
* Unless required by what the review is changing/adding, it is a best practice to
  create separate issues for changes which can be handled independently from
  the current MR to avoid slowing it down (preferably with a plan of who will
  follow up on these newly-created issues and when). See "Resolve comment with
  issue" below.

For both roles, try to respond to activity in a timely (e.g. 1-3 days) fashion,
which could include:

* Leaving feedback
* Making changes from feedback
* Reaching out to the author/reviewer directly if it may take some time to get
  to the review/activity
* Pontentially assigning someone else for the review (if someone else is better
  suited)

Similarly, if a review may require a large amount of rework, it may be useful to either:

* Mark as a draft until "ready" again
* Close and create a new MR when ready
