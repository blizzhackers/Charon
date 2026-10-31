#include "headers/feature.h"

namespace Async {


    class AsyncFeature : Feature {
        std::list<Task::Task<>> tasks;
        bool inGame = false;
        uint32_t frame = 0;

        void gameLoop() override {
            if (!this->inGame) {
                this->inGame = true;
                this->frame = 0;
            }

            // Start all the async methods after 1 frame, so it gives all the features the ability to do stuff in the first frame
            if (this->frame++ == 1) {
                for (Feature *f = Features; f; f = f->next) {
                    auto promise = f->async();
                    this->tasks.push_back(std::move(promise));
                }
                return;
            }

            // Run the async code
            auto it = this->tasks.begin();
            while (it != this->tasks.end()) {
                bool isDone = (*it).resume();
                if (isDone) {
                    it = this->tasks.erase(it);
                } else {
                    it++;
                }
            }

        }

        void oogLoop() override {
            this->inGame = false;

            // ToDo; clear up the tasks properly?
            this->tasks.clear();
        }

    } feature;

}